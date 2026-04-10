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
	"sync"
	"time"

	"github.com/joho/godotenv"
	_ "github.com/mattn/go-sqlite3" // SQLite driver
)

// --- Server & Security Constants ---
const (
	Port              = "8000"
	DBPath            = "scores.db"
	MaxActiveSessions = 5000 // Anti-OOM: Global limit of concurrent players
	MaxReqPerSec      = 20   // Anti-Spam: 20 requests per second max per IP
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
	Score       int
	FruitsEaten int
	LastPing    time.Time
	Cheated     bool
	Seed        uint32
	HeadX       int
	HeadY       int
	LastSteps   int
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
	// 1. Set hardcoded base default values
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

	// 2. Override with external JSON file if available
	file, err := os.Open("rules.json")
	if err == nil {
		defer file.Close()
		if err := json.NewDecoder(file).Decode(&Rules); err == nil {
			log.Println("[Server] Custom game rules loaded from rules.json")
		} else {
			log.Printf("[Server] Error parsing rules.json, relying on defaults: %v", err)
		}
	}

	// 3. Dynamically calculate the maximum possible score (used internally for security)
	Rules.MaxScore = Rules.GameWidth * Rules.GameHeight * Rules.PointsPerFruit

	// 4. Log active configuration for observability
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
	sessionMutex.Lock()
	for token, session := range activeSessions {
		if time.Since(session.LastPing).Seconds() > 900 {
			delete(activeSessions, token)
		}
	}
	sessionMutex.Unlock()

	ipMutex.Lock()
	for ip, data := range ipDataMap {
		if time.Since(data.WindowStart) > 5*time.Minute {
			delete(ipDataMap, ip)
		}
	}
	ipMutex.Unlock()
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

// --- API Handlers ---

func handleRules(w http.ResponseWriter, r *http.Request) {
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
		Score:       0,
		FruitsEaten: 0,
		LastPing:    time.Now(),
		Cheated:     false,
		Seed:        currentSeed,
		HeadX:       (Rules.GameWidth / 2) - offsetX,
		HeadY:       (Rules.GameHeight / 2) - offsetY,
		LastSteps:   0,
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
	if !exists || session.Cheated {
		sessionMutex.Unlock()
		http.Error(w, "Unauthorized", http.StatusForbidden)
		return
	}

	deltaSteps := steps - session.LastSteps
	if deltaSteps <= 0 {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] CHEAT (Steps): %s | Steps did not increase (%d)", ip, token[:8], steps)
		http.Error(w, "Invalid steps", http.StatusBadRequest)
		return
	}

	// 1. RNG Validation
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
		log.Printf("[%s] CHEAT (RNG): %s | Client fruit at (%d,%d) not in server sequence.", ip, token[:8], fx, fy)
		http.Error(w, "Invalid fruit", http.StatusBadRequest)
		return
	}

	// 2. Physical Validation (Manhattan Distance)
	manhattan := int(math.Abs(float64(fx-session.HeadX)) + math.Abs(float64(fy-session.HeadY)))
	if deltaSteps < manhattan {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] CHEAT (Movement): %s | %d steps taken for dist %d", ip, token[:8], deltaSteps, manhattan)
		http.Error(w, "Impossible move", http.StatusBadRequest)
		return
	}

	// 3. The Time Corridor (Anti-Speedhack & Anti-Pause)
	// Calculate the speed active DURING the travel (before current fruit acceleration)
	currentDelaySec := (Rules.InitialDelay / 1000000.0) * math.Pow(Rules.SpeedupFactor, float64(session.FruitsEaten))
	expectedTime := currentDelaySec * float64(deltaSteps)

	actualTime := time.Since(session.LastPing).Seconds()
	minTime := math.Max(0, (expectedTime*0.75)-0.5)                        // Floor (Speedhack)
	maxTime := expectedTime + (float64(Rules.CheatTimeout) / 1000.0) + 5.0 // Ceiling (LLDB/Pause)

	if actualTime < minTime {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] CHEAT (Time): %s | Speedhack. Expected ~%.1fs, took %.1fs", ip, token[:8], expectedTime, actualTime)
		http.Error(w, "Speedhack detected", http.StatusBadRequest)
		return
	}

	if actualTime > maxTime {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] CHEAT (Timeout): %s | Paused/Lag. Expected ~%.1fs, took %.1fs", ip, token[:8], expectedTime, actualTime)
		http.Error(w, "Timeout detected", http.StatusBadRequest)
		return
	}

	// 4. Update Score & Penalties
	applyPenalties(session, steps)
	session.Score += Rules.PointsPerFruit
	session.FruitsEaten++

	// Check against dynamic MaxScore (strictly greater than)
	if session.Score > Rules.MaxScore {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] CHEAT (Limit): %s | Score %d exceeded capacity.", ip, token[:8], session.Score)
		http.Error(w, "Score limit exceeded", http.StatusBadRequest)
		return
	}

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

func handleSubmit(w http.ResponseWriter, r *http.Request) {
	token := r.PathValue("token")
	name := r.PathValue("name")
	steps, _ := strconv.Atoi(r.PathValue("steps"))
	ip := getRemoteIP(r)

	sessionMutex.Lock()
	session, exists := activeSessions[token]
	if exists {
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

	if session.Cheated {
		log.Printf("[%s] BANNED SUBMISSION: Player '%s' attempted to submit from flagged session %s.", ip, name, token[:8])
		session.Score = 0
	} else if session.Score > 0 {
		applyPenalties(session, steps)
	}

	finalScore := session.Score
	if finalScore < 0 || finalScore > Rules.MaxScore {
		log.Printf("[%s] SUBMIT REJECTED: Impossible score %d for '%s' (Session: %s).", ip, finalScore, name, token[:8])
		finalScore = 0
	}

	if finalScore > 0 {
		_, err := db.Exec("INSERT INTO scores (name, score) VALUES (?, ?)", name, finalScore)
		if err != nil {
			log.Printf("[%s] Database error: %v", ip, err)
			http.Error(w, "Backend Error", http.StatusInternalServerError)
			return
		}
		log.Printf("[%s] SCORE SAVED: %s | Player: '%s' | Score: %d | Fruits: %d", ip, token[:8], name, finalScore, session.FruitsEaten)
	} else {
		log.Printf("[%s] SCORE IGNORED: %s | Player: '%s' processed but not saved (Zero or Cheated).", ip, token[:8], name)
	}

	fmt.Fprint(w, "OK")
}

func handleScores(w http.ResponseWriter, r *http.Request) {
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
	mux.HandleFunc("GET /submit/{token}/{name}/{steps}", handleSubmit)
	mux.HandleFunc("GET /scores/{limit}", handleScores)

	handler := rateLimitMiddleware(mux)

	log.Printf("[Server] Starting secure backend on 0.0.0.0:%s", Port)
	if err := http.ListenAndServe("0.0.0.0:"+Port, handler); err != nil {
		log.Fatalf("Server crashed: %v", err)
	}
}
