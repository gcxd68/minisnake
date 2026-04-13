package main

import (
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"math"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/joho/godotenv"
	_ "github.com/mattn/go-sqlite3" // SQLite driver
)

// --- Server & Security Constants ---
const (
	Port                  = "8000"
	DBPath                = "scores.db"
	MaxActiveSessions     = 5000 // Anti-OOM: Global limit of concurrent players
	MaxReqPerSec          = 20   // Anti-Spam: 20 requests per second max per IP
	RequiredClientVersion = "1"  // Required client version
)

// --- Dynamic Game Rules Structure ---
type GameRules struct {
	GameWidth             int
	GameHeight            int
	InitialDelay          float64
	SpeedupFactor         float64
	PointsPerFruit        int
	CheatTimeout          int
	InitialSize           int
	PenaltyInterval       int
	PenaltyAmount         int
	SpawnFruitMaxAttempts int
	MaxScore              int `json:"-"` // Calculated dynamically to prevent security override
}

// --- Game Structures ---
type Session struct {
	Score        int
	FruitsEaten  int
	LastPing     time.Time
	Cheated      bool
	Seed         uint32
	HeadX        int
	HeadY        int
	LastSteps    int
	PerfectPaths int
}

// Security Tracking per IP (Rate limiting only, no bans)
type IPData struct {
	ReqCount    int
	WindowStart time.Time
}

// Global state
var (
	Rules          GameRules
	db             *sql.DB
	activeSessions = make(map[string]*Session)
	sessionMutex   sync.RWMutex

	ipDataMap = make(map[string]*IPData)
	ipMutex   sync.Mutex
)

// --- Configuration Logic ---

// loadRules: Initializes default game rules and overrides them if rules.json exists
func loadRules() {
	Rules = GameRules{
		GameWidth:             25,
		GameHeight:            20,
		InitialDelay:          250000.0,
		SpeedupFactor:         0.985,
		PointsPerFruit:        10,
		CheatTimeout:          5000,
		InitialSize:           1,
		PenaltyInterval:       10,
		PenaltyAmount:         1,
		SpawnFruitMaxAttempts: 10000,
	}

	file, err := os.Open("rules.json")
	if err == nil {
		defer file.Close()
		if err := json.NewDecoder(file).Decode(&Rules); err == nil {
			log.Println("[Server] Custom game rules loaded from rules.json")
		} else {
			log.Printf("[Server] Error parsing rules.json, relying on defaults: %v", err)
		}
	}

	// Dynamically calculate the maximum possible score (used internally for security)
	Rules.MaxScore = Rules.GameWidth * Rules.GameHeight * Rules.PointsPerFruit

	log.Printf("[Server] Active Rules: %dx%d | Delay: %.0f | Speedup: %.3f | PPF: %d | Size: %d | Penalty: %d | Interval: %d",
		Rules.GameWidth, Rules.GameHeight, Rules.InitialDelay, Rules.SpeedupFactor,
		Rules.PointsPerFruit, Rules.InitialSize, Rules.PenaltyAmount, Rules.PenaltyInterval)
}

// --- Helper Functions ---

// lcgRand: Pseudo-random generator synchronized with the C client
func lcgRand(seed uint32) uint32 {
	return (seed*1103515245 + 12345) & 0x7fffffff
}

// isAlphanumeric: Simple XSS prevention for the player's name
func isAlphanumeric(s string) bool {
	for _, r := range s {
		if (r < 'a' || r > 'z') && (r < 'A' || r > 'Z') && (r < '0' || r > '9') {
			return false
		}
	}
	return true
}

// applyPenalties: Deducts points if the player takes too many steps without eating
func applyPenalties(session *Session, newSteps int) {
	if Rules.PenaltyInterval <= 0 {
		return
	}
	for step := session.LastSteps + 1; step <= newSteps; step++ {
		if step%Rules.PenaltyInterval == 0 {
			session.Score = max(0, session.Score-Rules.PenaltyAmount)
		}
	}
}

// getRemoteIP: Extracts the client IP for logging
func getRemoteIP(r *http.Request) string {
	ip, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return ip
}

// --- Middleware & Security ---

func rateLimitMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		ip := getRemoteIP(r)

		ipMutex.Lock()
		data, exists := ipDataMap[ip]
		if !exists {
			data = &IPData{WindowStart: time.Now()}
			ipDataMap[ip] = data
		}

		if time.Since(data.WindowStart) > time.Second {
			data.ReqCount = 0
			data.WindowStart = time.Now()
		}
		data.ReqCount++
		if data.ReqCount > MaxReqPerSec {
			ipMutex.Unlock()
			log.Printf("[SECURITY] SPAM BLOCKED from IP %s", ip)
			http.Error(w, "Too Many Requests", http.StatusTooManyRequests)
			return
		}
		ipMutex.Unlock()

		next.ServeHTTP(w, r)
	})
}

func cleanupStaleData() {
	var removedTokens []string

	sessionMutex.Lock()
	for token, session := range activeSessions {
		// Removes sessions inactive for > 15 minutes
		if time.Since(session.LastPing).Seconds() > 900 {
			removedTokens = append(removedTokens, token[:8])
			delete(activeSessions, token)
		}
	}
	sessionMutex.Unlock()

	removedIPs := 0
	ipMutex.Lock()
	for ip, data := range ipDataMap {
		// Removes IP records older than 5 minutes
		if time.Since(data.WindowStart) > 5*time.Minute {
			delete(ipDataMap, ip)
			removedIPs++
		}
	}
	ipMutex.Unlock()

	if len(removedTokens) > 0 || removedIPs > 0 {
		if len(removedTokens) > 0 {
			log.Printf("[CLEANUP] Swept %d ghost sessions (%s) and %d stale IP records.", len(removedTokens), strings.Join(removedTokens, ", "), removedIPs)
		} else {
			log.Printf("[CLEANUP] Swept 0 ghost sessions and %d stale IP records.", removedIPs)
		}
	}
}

func initDB() {
	var err error
	db, err = sql.Open("sqlite3", DBPath)
	if err != nil {
		log.Fatalf("Database opening failed: %v", err)
	}

	query := `CREATE TABLE IF NOT EXISTS scores (
		name TEXT, score INTEGER, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
	)`
	if _, err = db.Exec(query); err != nil {
		log.Fatalf("Database table creation failed: %v", err)
	}
	log.Println("[Server] Database initialized successfully.")
}

// --- Anti-Bot Detection ---

// isBotFingerprint evaluates the HTTP fingerprint to detect automated scripts.
func isBotFingerprint(r *http.Request) bool {
	// =========================================================================
	// WARNING: PROTOCOL UPGRADE TRAP
	// The strict HTTP/1.0 check below ONLY works because this server runs
	// directly on a raw GCE e2-micro instance without any proxy.
	// If you ever place this server behind a Reverse Proxy, Cloudflare, or an
	// L7 Load Balancer, the proxy will upgrade the protocol to HTTP/1.1 or HTTP/2.
	// If that happens, this check will shadowban ALL legitimate players.
	// REMOVE THIS CHECK IF YOUR INFRASTRUCTURE CHANGES.
	// =========================================================================
	if r.Proto != "HTTP/1.0" {
		return true
	}

	// Ensure absence of typical headers injected by modern HTTP libraries.
	if r.Header.Get("User-Agent") != "" ||
		r.Header.Get("Accept") != "" ||
		r.Header.Get("Accept-Encoding") != "" ||
		r.Header.Get("Accept-Language") != "" {
		return true
	}

	// Ensure the connection is explicitly closed, matching the C client's behavior.
	if strings.ToLower(r.Header.Get("Connection")) == "keep-alive" {
		return true
	}

	return false
}

// flagIfBotFingerprint acts as a centralized helper to dry up shadowban logic.
func flagIfBotFingerprint(r *http.Request, session *Session, ip, token, routeContext string) bool {
	if isBotFingerprint(r) {
		session.Cheated = true
		log.Printf("[%s] SHADOWBAN (Fingerprint/%s): %s caught using non-C headers.", ip, routeContext, token[:8])
		return true
	}
	return false
}

// --- API Handlers ---

func handleRules(w http.ResponseWriter, r *http.Request) {
	version := r.Header.Get("X-Client-Version")

	// If the client identifies itself (v1.0+) but the version is outdated.
	// Legacy clients (v0.5) send an empty string and will pass this check
	// to be caught later by the leaderboard logic.
	if version != "" && version != RequiredClientVersion {
		fmt.Fprint(w, "UPDATE")
		return
	}

	// Send game rules to valid v1.0+ clients and legacy v0.5 clients
	fmt.Fprintf(w, "%d|%d|%d|%f|%d|%d|%d|%d|%d|%d",
		Rules.GameWidth, Rules.GameHeight, int(Rules.InitialDelay), Rules.SpeedupFactor, Rules.PointsPerFruit,
		Rules.CheatTimeout, Rules.InitialSize, Rules.PenaltyInterval, Rules.PenaltyAmount, Rules.SpawnFruitMaxAttempts)
}

func handleToken(w http.ResponseWriter, r *http.Request) {
	ip := getRemoteIP(r)

	sessionMutex.RLock()
	if len(activeSessions) >= MaxActiveSessions {
		sessionMutex.RUnlock()
		log.Printf("[SECURITY] OOM LIMIT REACHED. Refused connection for %s", ip)
		http.Error(w, "Server full", http.StatusServiceUnavailable)
		return
	}
	sessionMutex.RUnlock()

	bytes := make([]byte, 16)
	rand.Read(bytes)
	token := hex.EncodeToString(bytes)

	seedBytes := make([]byte, 4)
	rand.Read(seedBytes)
	originalSeed := (uint32(seedBytes[0])<<24 | uint32(seedBytes[1])<<16 | uint32(seedBytes[2])<<8 | uint32(seedBytes[3])) & 0x7fffffff

	currentSeed := originalSeed
	offsetX := 0
	if Rules.GameWidth%2 == 0 {
		currentSeed = lcgRand(currentSeed)
		offsetX = int(currentSeed % 2)
	}

	offsetY := 0
	if Rules.GameHeight%2 == 0 {
		currentSeed = lcgRand(currentSeed)
		offsetY = int(currentSeed % 2)
	}

	sessionMutex.Lock()
	activeSessions[token] = &Session{
		Score:        0,
		FruitsEaten:  0,
		LastPing:     time.Now(),
		Cheated:      false,
		Seed:         currentSeed,
		HeadX:        (Rules.GameWidth / 2) - offsetX,
		HeadY:        (Rules.GameHeight / 2) - offsetY,
		LastSteps:    0,
		PerfectPaths: 0,
	}
	sessionMutex.Unlock()

	log.Printf("[%s] SESSION START: Token %s... | Seed: %d", ip, token[:8], originalSeed)
	fmt.Fprintf(w, "%s|%d", token, originalSeed)
}

func handleEat(w http.ResponseWriter, r *http.Request) {
	token := r.PathValue("token")
	steps, _ := strconv.Atoi(r.PathValue("steps"))
	fx, _ := strconv.Atoi(r.PathValue("fx"))
	fy, _ := strconv.Atoi(r.PathValue("fy"))
	ip := getRemoteIP(r)

	sessionMutex.Lock()
	session, exists := activeSessions[token]

	// 0. General Session Check
	// If the session doesn't exist or is already flagged, maintain the illusion.
	if !exists || session.Cheated {
		sessionMutex.Unlock()
		fmt.Fprint(w, "Yum")
		return
	}

	// 1. Fingerprint Validation (Anti-Bot)
	if flagIfBotFingerprint(r, session, ip, token, "Eat") {
		sessionMutex.Unlock()
		fmt.Fprint(w, "Yum")
		return
	}

	// 2. Step Validation (Anti-Replay)
	// Ensure the step counter strictly increases.
	deltaSteps := steps - session.LastSteps
	if deltaSteps <= 0 {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] SHADOWBAN (Steps): %s | Steps did not increase (%d)", ip, token[:8], steps)
		fmt.Fprint(w, "Yum")
		return
	}

	// 3. RNG Validation
	validFruit := false
	tempSeed := session.Seed
	for i := 0; i < Rules.SpawnFruitMaxAttempts; i++ {
		tempSeed = lcgRand(tempSeed)
		candX := int((tempSeed >> 16) % uint32(Rules.GameWidth))
		tempSeed = lcgRand(tempSeed)
		candY := int((tempSeed >> 16) % uint32(Rules.GameHeight))

		if candX == fx && candY == fy {
			validFruit = true
			session.Seed = tempSeed
			break
		}
	}

	if !validFruit {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] SHADOWBAN (RNG): %s | Client fruit at (%d,%d) not in server sequence.", ip, token[:8], fx, fy)
		fmt.Fprint(w, "Yum")
		return
	}

	// 4. Physical Validation (Manhattan Distance)
	manhattan := int(math.Abs(float64(fx-session.HeadX)) + math.Abs(float64(fy-session.HeadY)))
	if deltaSteps < manhattan {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] SHADOWBAN (Movement): %s | %d steps taken for dist %d", ip, token[:8], deltaSteps, manhattan)
		fmt.Fprint(w, "Yum")
		return
	}

	// 5. Behavioral Validation (The Manhattan Anomaly)
	if deltaSteps == manhattan {
		session.PerfectPaths++
	}

	// Evaluate behavioral consistency only after the snake is long enough
	currentFruits := session.FruitsEaten + 1
	if currentFruits >= 20 {
		// Calculate percentage using integer math
		perfRatio := (session.PerfectPaths * 100) / currentFruits

		// 95% perfect paths on a long snake is impossible for a human
		if perfRatio >= 95 {
			session.Cheated = true
			sessionMutex.Unlock()
			log.Printf("[%s] SHADOWBAN (Behavior): %s | UgoBot caught! %d%% perfect paths over %d fruits.", ip, token[:8], perfRatio, currentFruits)
			fmt.Fprint(w, "Yum")
			return
		}
	}

	// 6. The Time Corridor (Anti-Speedhack & Anti-Pause)
	// Calculate the speed active DURING the travel (before current fruit acceleration)
	currentDelaySec := (Rules.InitialDelay / 1000000.0) * math.Pow(Rules.SpeedupFactor, float64(session.FruitsEaten))
	expectedTime := currentDelaySec * float64(deltaSteps)

	actualTime := time.Since(session.LastPing).Seconds()

	// Absorbs 1s of fixed network lag (packet bunching), but remains strict (75%) on long distances
	minTime := math.Max(0, (expectedTime*0.75)-1.0)                        // Floor (Speedhack)
	maxTime := expectedTime + (float64(Rules.CheatTimeout) / 1000.0) + 5.0 // Ceiling (LLDB/Pause)

	if actualTime < minTime {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] SHADOWBAN (Time): %s | Speedhack. Expected ~%.1fs, took %.1fs", ip, token[:8], expectedTime, actualTime)
		fmt.Fprint(w, "Yum")
		return
	}

	if actualTime > maxTime {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] SHADOWBAN (Timeout): %s | Paused/Lag. Expected ~%.1fs, took %.1fs", ip, token[:8], expectedTime, actualTime)
		fmt.Fprint(w, "Yum")
		return
	}

	// 7. Update Score & Penalties
	applyPenalties(session, steps)
	session.Score += Rules.PointsPerFruit
	session.FruitsEaten++

	// 8. Check against dynamic MaxScore (strictly greater than)
	if session.Score > Rules.MaxScore {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] SHADOWBAN (Limit): %s | Score %d exceeded capacity.", ip, token[:8], session.Score)
		fmt.Fprint(w, "Yum")
		return
	}

	// Update State
	session.LastPing = time.Now()
	session.HeadX, session.HeadY, session.LastSteps = fx, fy, steps
	sessionMutex.Unlock()

	fmt.Fprint(w, "Yum")
}

func handleCheat(w http.ResponseWriter, r *http.Request) {
	ip := getRemoteIP(r)
	token := r.PathValue("token")

	sessionMutex.Lock()
	if session, exists := activeSessions[token]; exists {
		session.Cheated = true
	}
	sessionMutex.Unlock()

	log.Printf("[%s] CHEAT (Local): %s | TracerPid/Local anomaly reported by client.", ip, token[:8])
	fmt.Fprint(w, "Flagged")
}

// handleQuit: Instantly frees memory when a player voluntarily skips submission
func handleQuit(w http.ResponseWriter, r *http.Request) {
	token := r.PathValue("token")
	ip := getRemoteIP(r)

	sessionMutex.Lock()
	_, exists := activeSessions[token]
	if exists {
		delete(activeSessions, token)
	}
	sessionMutex.Unlock()

	if exists {
		log.Printf("[%s] SESSION ABANDONED: Token %s dropped gracefully by client.", ip, token[:8])
	}
	fmt.Fprint(w, "OK")
}

func handleSubmit(w http.ResponseWriter, r *http.Request) {
	token := r.PathValue("token")
	name := r.PathValue("name")
	steps, _ := strconv.Atoi(r.PathValue("steps"))
	ip := getRemoteIP(r)

	sessionMutex.Lock()
	session, exists := activeSessions[token]
	if exists {
		// Session is safely removed from active tracking upon submission attempt
		delete(activeSessions, token)
	}
	sessionMutex.Unlock()

	if !exists {
		log.Printf("[%s] SUBMIT REJECTED: Expired or invalid token %s", ip, token[:8])
		fmt.Fprint(w, "OK")
		return
	}

	if len(name) == 0 || len(name) > 8 || !isAlphanumeric(name) {
		log.Printf("[%s] SUBMIT REJECTED: Invalid name '%s' from %s", ip, name, token[:8])
		fmt.Fprint(w, "OK")
		return
	}

	// Legacy Client Trap (Protects database from old unversioned clients)
	if r.Header.Get("X-Client-Version") != RequiredClientVersion {
		log.Printf("[%s] SCORE IGNORED: %s | Player: '%s' | Reason: Legacy Client Version", ip, token[:8], name)
		fmt.Fprint(w, "OK") // Return 200 OK so they still proceed to the Leaderboard screen
		return
	}

	// Fingerprint Validation (Anti-Bot)
	// Catches bots that bypass /eat and only submit raw scores
	flagIfBotFingerprint(r, session, ip, token, "Submit")

	// Apply game rules and movement penalties
	if session.Cheated {
		session.Score = 0 // Let anti-cheat crush the score silently
	} else if session.Score > 0 {
		applyPenalties(session, steps)
	}

	finalScore := session.Score
	reason := "Score is Zero" // Default reason for a legitimate rejection

	// Final anomaly detection to provide an explicit log reason
	if session.Cheated {
		reason = "Cheating Detected"
	} else if finalScore < 0 || finalScore > Rules.MaxScore {
		log.Printf("[%s] SUBMIT REJECTED: Impossible score %d for '%s' (Session: %s).", ip, finalScore, name, token[:8])
		finalScore = 0
		reason = "Impossible Score Limit"
	}

	// Save to database or ignore with the explicit reason
	if finalScore > 0 {
		_, err := db.Exec("INSERT INTO scores (name, score) VALUES (?, ?)", name, finalScore)
		if err != nil {
			log.Printf("[%s] Database error: %v", ip, err)
			http.Error(w, "Backend Error", http.StatusInternalServerError)
			return
		}
		log.Printf("[%s] SCORE SAVED: %s | Player: '%s' | Score: %d | Fruits: %d", ip, token[:8], name, finalScore, session.FruitsEaten)
	} else {
		log.Printf("[%s] SCORE IGNORED: %s | Player: '%s' | Reason: %s", ip, token[:8], name, reason)
	}

	fmt.Fprint(w, "OK")
}

func handleScores(w http.ResponseWriter, r *http.Request) {
	// Payload Hijacking for Legacy Clients
	if r.Header.Get("X-Client-Version") != RequiredClientVersion {
		fmt.Fprint(w, "UPDATE REQUIRED!| |0|0\n")
		fmt.Fprint(w, " | |0|0\n")
		fmt.Fprint(w, "PLEASE GET THE | |0|0\n")
		fmt.Fprint(w, "LATEST VERSION AT: | |0|0\n")
		fmt.Fprint(w, " | |0|0\n")
		fmt.Fprint(w, "GITHUB.COM/ | |0|0\n")
		fmt.Fprint(w, "GCXD68/     | |0|0\n")
		fmt.Fprint(w, "MINISNAKE/  | |0|0\n")
		fmt.Fprint(w, "RELEASES    | |0|0\n")
		return
	}

	limit, _ := strconv.Atoi(r.PathValue("limit"))
	limit = max(1, min(limit, 100))

	rows, err := db.Query("SELECT name, score FROM scores ORDER BY score DESC, timestamp ASC LIMIT ?", limit)
	if err != nil {
		log.Printf("[%s] Database error: %v", getRemoteIP(r), err)
		http.Error(w, "Backend Error", http.StatusInternalServerError)
		return
	}
	defer rows.Close()

	for rows.Next() {
		var name string
		var score int
		if err := rows.Scan(&name, &score); err == nil {
			fmt.Fprintf(w, "%s|%d|0|0\n", name, score)
		}
	}
}

func main() {
	godotenv.Load()
	loadRules()
	initDB()
	defer db.Close()

	go func() {
		ticker := time.NewTicker(5 * time.Minute)
		defer ticker.Stop()
		for range ticker.C {
			cleanupStaleData()
		}
	}()

	mux := http.NewServeMux()
	mux.HandleFunc("GET /rules", handleRules)
	mux.HandleFunc("GET /token", handleToken)
	mux.HandleFunc("GET /eat/{token}/{steps}/{fx}/{fy}", handleEat)
	mux.HandleFunc("GET /cheat/{token}", handleCheat)
	mux.HandleFunc("GET /quit/{token}", handleQuit)
	mux.HandleFunc("GET /submit/{token}/{name}/{steps}", handleSubmit)
	mux.HandleFunc("GET /scores/{limit}", handleScores)

	handler := rateLimitMiddleware(mux)

	log.Printf("[Server] Starting secure backend on 0.0.0.0:%s", Port)
	if err := http.ListenAndServe("0.0.0.0:"+Port, handler); err != nil {
		log.Fatalf("Server crashed: %v", err)
	}
}
