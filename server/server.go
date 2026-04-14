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
	_ "github.com/mattn/go-sqlite3"
)

// --- Server & Security Constants ---
const (
	DefaultPort           = "8000"
	DBPath                = "scores.db"
	MaxActiveSessions     = 5000
	MaxReqPerSec          = 20
	RequiredClientVersion = "2"
)

// --- Structures ---
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
	MaxScore              int `json:"-"`
}

type Point struct {
	X int
	Y int
}

type Session struct {
	Score         int
	FruitsEaten   int
	StartTime     time.Time // Global Speed Check
	LastPing      time.Time
	Cheated       bool
	Seed          uint32
	HeadX         int
	HeadY         int
	LastSteps     int
	PerfectPaths  int
	TotalSteps    int
	RecentDetours []int // Variance Telemetry (Sliding Window)
	TargetFruit   Point // Opaque Server-Side RNG
	
	// HEAVY PHYSICS FIELDS
	ExpectedSeq   int
	Grid          []bool  // O(1) Spatial Hashing grid to detect collisions instantly
	Grow          int     // Pending growth counter
	Body          []Point // Full authoritative body tracking
}

type EatPayload struct {
	Seq   int    `json:"seq"`
	Steps int    `json:"steps"`
	Fx    int    `json:"fx"`
	Fy    int    `json:"fy"`
	Path  string `json:"path"`
}

type IPData struct {
	ReqCount    int
	WindowStart time.Time
}

var (
	Rules          GameRules
	db             *sql.DB
	activeSessions = make(map[string]*Session)
	sessionMutex   sync.RWMutex
	ipDataMap      = make(map[string]*IPData)
	ipMutex        sync.Mutex
)

// --- Configuration & Initialization ---

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
		json.NewDecoder(file).Decode(&Rules)
	}
	Rules.MaxScore = Rules.GameWidth * Rules.GameHeight * Rules.PointsPerFruit
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
}

// --- Helpers ---

func lcgRand(seed uint32) uint32 {
	return (seed*1103515245 + 12345) & 0x7fffffff
}

func isAlphanumeric(s string) bool {
	for _, r := range s {
		if (r < 'a' || r > 'z') && (r < 'A' || r > 'Z') && (r < '0' || r > '9') {
			return false
		}
	}
	return true
}

func getRemoteIP(r *http.Request) string {
	ip, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return ip
}

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

// spawnFruit generates the next target fruit server-side.
// OPTIMIZATION: It uses the O(1) boolean Grid to instantly verify that the 
// new fruit does not spawn on top of any part of the snake's body.
func spawnFruit(session *Session, prevX, prevY int) {
	for i := 0; i < Rules.SpawnFruitMaxAttempts; i++ {
		session.Seed = lcgRand(session.Seed)
		candX := int((session.Seed >> 16) % uint32(Rules.GameWidth))
		session.Seed = lcgRand(session.Seed)
		candY := int((session.Seed >> 16) % uint32(Rules.GameHeight))

		gridIndex := candY*Rules.GameWidth + candX

		// Ensure the fruit is NOT on the snake's body AND NOT on the previous fruit's exact spot
		if !session.Grid[gridIndex] && (candX != prevX || candY != prevY) {
			session.TargetFruit = Point{X: candX, Y: candY}
			return
		}
	}
}

// --- Middlewares & Anti-Bot ---

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
			http.Error(w, "Too Many Requests", http.StatusTooManyRequests)
			return
		}
		ipMutex.Unlock()
		next.ServeHTTP(w, r)
	})
}

func flagIfBotFingerprint(r *http.Request, session *Session, ip, token string) bool {
	if r.Proto != "HTTP/1.0" || r.Header.Get("User-Agent") != "" || r.Header.Get("Accept") != "" {
		session.Cheated = true
		log.Printf("[SHADOWBAN_FINGERPRINT] token=%s ip=%s", token, ip)
		return true
	}
	return false
}

func cleanupStaleData() {
	sessionMutex.Lock()
	for token, session := range activeSessions {
		if time.Since(session.LastPing).Seconds() > 900 {
			delete(activeSessions, token)
		}
	}
	sessionMutex.Unlock()
}

// --- API Handlers ---

func handleRules(w http.ResponseWriter, r *http.Request) {
	if r.Header.Get("X-Client-Version") != "" && r.Header.Get("X-Client-Version") != RequiredClientVersion {
		fmt.Fprint(w, "UPDATE")
		return
	}
	fmt.Fprintf(w, "%d|%d|%d|%f|%d|%d|%d|%d|%d|%d",
		Rules.GameWidth, Rules.GameHeight, int(Rules.InitialDelay), Rules.SpeedupFactor, Rules.PointsPerFruit,
		Rules.CheatTimeout, Rules.InitialSize, Rules.PenaltyInterval, Rules.PenaltyAmount, Rules.SpawnFruitMaxAttempts)
}

func handleToken(w http.ResponseWriter, r *http.Request) {
	ip := getRemoteIP(r)

	sessionMutex.RLock()
	if len(activeSessions) >= MaxActiveSessions {
		sessionMutex.RUnlock()
		http.Error(w, "Server full", http.StatusServiceUnavailable)
		return
	}
	sessionMutex.RUnlock()

	bytes := make([]byte, 16)
	rand.Read(bytes)
	token := hex.EncodeToString(bytes)

	seedBytes := make([]byte, 4)
	rand.Read(seedBytes)
	currentSeed := (uint32(seedBytes[0])<<24 | uint32(seedBytes[1])<<16 | uint32(seedBytes[2])<<8 | uint32(seedBytes[3])) & 0x7fffffff

	offsetX, offsetY := 0, 0
	if Rules.GameWidth%2 == 0 {
		currentSeed = lcgRand(currentSeed)
		offsetX = int(currentSeed % 2)
	}
	if Rules.GameHeight%2 == 0 {
		currentSeed = lcgRand(currentSeed)
		offsetY = int(currentSeed % 2)
	}

	headX := (Rules.GameWidth / 2) - offsetX
	headY := (Rules.GameHeight / 2) - offsetY
	head := Point{X: headX, Y: headY}

	// Initialize the O(1) tracking grid
	grid := make([]bool, Rules.GameWidth*Rules.GameHeight)
	grid[headY*Rules.GameWidth+headX] = true

	now := time.Now()
	session := &Session{
		Score:         0,
		FruitsEaten:   0,
		StartTime:     now,
		LastPing:      now,
		Cheated:       false,
		Seed:          currentSeed,
		HeadX:         headX,
		HeadY:         headY,
		LastSteps:     0,
		PerfectPaths:  0,
		TotalSteps:    0,
		RecentDetours: make([]int, 0, 30),
		ExpectedSeq:   1,
		Grid:          grid,
		Grow:          Rules.InitialSize - 1,
		Body:          []Point{head},
	}
	spawnFruit(session, -1, -1) // Opaque RNG: Server dictates the first fruit

	sessionMutex.Lock()
	activeSessions[token] = session
	sessionMutex.Unlock()

	log.Printf("[SESSION_START] token=%s ip=%s", token, ip)
	fmt.Fprintf(w, "%s|%d|%d", token, session.TargetFruit.X, session.TargetFruit.Y)
}

func handleEat(w http.ResponseWriter, r *http.Request) {
	token := r.PathValue("token")
	ip := getRemoteIP(r)

	// 1. Decode the JSON POST Payload
	var payload EatPayload
	if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
		http.Error(w, "Bad Request", http.StatusBadRequest)
		return
	}

	sessionMutex.Lock()
	session, exists := activeSessions[token]

	// Helper: Generates a believable fake fruit and advances the core seed
	sendFakeFruit := func() {
		session.Seed = lcgRand(session.Seed)
		fakeX := int((session.Seed >> 16) % uint32(Rules.GameWidth))
		session.Seed = lcgRand(session.Seed)
		fakeY := int((session.Seed >> 16) % uint32(Rules.GameHeight))
		fmt.Fprintf(w, "%d|%d", fakeX, fakeY)
	}

	if !exists || session.Cheated || flagIfBotFingerprint(r, session, ip, token) {
		sessionMutex.Unlock()
		if exists && session.Cheated {
			sendFakeFruit()
		} else {
			fmt.Fprint(w, "0|0")
		}
		return
	}

	// 2. Core Validations (Read Only)
	deltaSteps := payload.Steps - session.LastSteps
	if deltaSteps <= 0 {
		session.Cheated = true
	}

	// 3. HEAVY PHYSICS: O(P) Simulation using Spatial Hashing
	// Replay the client's path and validate every single step instantly
	for _, move := range payload.Path {
		if len(session.Body) == 0 { break }

		newHead := session.Body[0]
		switch move {
		case 'U': newHead.Y--
		case 'D': newHead.Y++
		case 'L': newHead.X--
		case 'R': newHead.X++
		case ' ', 'X': continue // Ignore STOP instructions safely
		default: continue
		}

		// A. Wall Collision Check
		if newHead.X < 0 || newHead.X >= Rules.GameWidth || newHead.Y < 0 || newHead.Y >= Rules.GameHeight {
			session.Cheated = true
			log.Printf("[SHADOWBAN_PHYSICS] token=%s ip=%s | Wall collision at (%d,%d)", token, ip, newHead.X, newHead.Y)
			break
		}

		// B. Self Collision Check O(1)
		gridIndex := newHead.Y*Rules.GameWidth + newHead.X
		tail := session.Body[len(session.Body)-1]
		
		// Exception: It's valid if the snake's head enters the space the tail is currently leaving
		isTailMoving := (newHead.X == tail.X && newHead.Y == tail.Y && session.Grow == 0)

		if session.Grid[gridIndex] && !isTailMoving {
			session.Cheated = true
			log.Printf("[SHADOWBAN_PHYSICS] token=%s ip=%s | Self-collision at (%d,%d)", token, ip, newHead.X, newHead.Y)
			break
		}

		// C. Mutate State: Advance Head
		session.Body = append([]Point{newHead}, session.Body...)
		session.Grid[gridIndex] = true

		// D. Mutate State: Advance Tail or Grow
		if session.Grow > 0 {
			session.Grow--
		} else {
			oldTail := session.Body[len(session.Body)-1]
			session.Grid[oldTail.Y*Rules.GameWidth+oldTail.X] = false
			session.Body = session.Body[:len(session.Body)-1]
		}
	}

	// 4. Target Validation
	// Did the physical path actually land on the server-dictated fruit?
	if !session.Cheated {
		if len(session.Body) == 0 || session.Body[0].X != payload.Fx || session.Body[0].Y != payload.Fy || payload.Fx != session.TargetFruit.X || payload.Fy != session.TargetFruit.Y {
			session.Cheated = true
			log.Printf("[SHADOWBAN_TARGET] token=%s ip=%s expected=(%d,%d) got=(%d,%d) sim=(%d,%d)", 
				token[:8], ip, session.TargetFruit.X, session.TargetFruit.Y, payload.Fx, payload.Fy, session.Body[0].X, session.Body[0].Y)
		}
	}

	// 5. Local Time Corridor
	currentDelaySec := (Rules.InitialDelay / 1000000.0) * math.Pow(Rules.SpeedupFactor, float64(session.FruitsEaten))
	expectedTime := currentDelaySec * float64(deltaSteps)
	actualTime := time.Since(session.LastPing).Seconds()

	if actualTime < math.Max(0, (expectedTime*0.75)-1.0) || actualTime > expectedTime+(float64(Rules.CheatTimeout)/1000.0)+5.0 {
		session.Cheated = true
		log.Printf("[SHADOWBAN_TIME] token=%s ip=%s expected=%.1f actual=%.1f", token, ip, expectedTime, actualTime)
	}

	if session.Cheated {
		sessionMutex.Unlock()
		sendFakeFruit()
		return
	}

	// 6. Behavioral Analytics (Manhattan)
	manhattan := int(math.Abs(float64(payload.Fx-session.HeadX)) + math.Abs(float64(payload.Fy-session.HeadY)))
	detour := deltaSteps - manhattan
	if detour == 0 {
		session.PerfectPaths++
	}
	session.TotalSteps += deltaSteps

	session.RecentDetours = append(session.RecentDetours, detour)
	if len(session.RecentDetours) > 30 {
		session.RecentDetours = session.RecentDetours[1:]
	}

	currentFruits := session.FruitsEaten + 1

	if currentFruits >= 30 {
		perfRatio := (session.PerfectPaths * 100) / currentFruits
		if perfRatio >= 95 {
			session.Cheated = true
			log.Printf("[SHADOWBAN_BEHAVIOR] token=%s ip=%s perfect_ratio=%d", token, ip, perfRatio)
			sessionMutex.Unlock()
			sendFakeFruit()
			return
		}

		if len(session.RecentDetours) == 30 {
			sum := 0.0
			for _, v := range session.RecentDetours { sum += float64(v) }
			mean := sum / 30.0

			variance := 0.0
			for _, v := range session.RecentDetours { variance += math.Pow(float64(v)-mean, 2) }
			variance /= 30.0

			log.Printf("[TELEMETRY_VAR] token=%s fruits=%d mean=%.2f variance=%.2f ip=%s", token, currentFruits, mean, variance, ip)
		}
	}

	applyPenalties(session, payload.Steps)
	session.Score += Rules.PointsPerFruit
	session.FruitsEaten++
	session.Grow++ // Flag the snake to grow on its next set of physical steps

	if session.Score > Rules.MaxScore {
		session.Cheated = true
		sessionMutex.Unlock()
		sendFakeFruit()
		return
	}

	// Wrap up and generate next target, avoiding the snake's actual current body
	session.LastPing = time.Now()
	session.HeadX, session.HeadY, session.LastSteps = payload.Fx, payload.Fy, payload.Steps
	session.ExpectedSeq++
	
	spawnFruit(session, payload.Fx, payload.Fy)

	sessionMutex.Unlock()
	fmt.Fprintf(w, "%d|%d", session.TargetFruit.X, session.TargetFruit.Y)
}

func handleCheat(w http.ResponseWriter, r *http.Request) {
	token := r.PathValue("token")
	sessionMutex.Lock()
	if session, exists := activeSessions[token]; exists {
		session.Cheated = true
	}
	sessionMutex.Unlock()
	fmt.Fprint(w, "Flagged")
}

func handleQuit(w http.ResponseWriter, r *http.Request) {
	token := r.PathValue("token")
	sessionMutex.Lock()
	delete(activeSessions, token)
	sessionMutex.Unlock()
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
		delete(activeSessions, token)
	}
	sessionMutex.Unlock()

	if !exists {
		fmt.Fprint(w, "OK")
		return
	}
	if len(name) == 0 || len(name) > 8 || !isAlphanumeric(name) {
		fmt.Fprint(w, "OK")
		return
	}
	if r.Header.Get("X-Client-Version") != RequiredClientVersion {
		fmt.Fprint(w, "OK")
		return
	}

	// Global Sanity Check (Absolute Time Corridor)
	duration := time.Since(session.StartTime).Seconds()
	minExpected := float64(session.TotalSteps) * (Rules.InitialDelay / 1000000.0) * 0.50

	if session.TotalSteps > 0 && duration < minExpected {
		session.Cheated = true
		log.Printf("[SHADOWBAN_GLOBALSPEED] token=%s ip=%s duration=%.1f steps=%d min_expected=%.1f",
			token, ip, duration, session.TotalSteps, minExpected)
	}

	if session.Cheated {
		session.Score = 0
	} else if session.Score > 0 {
		applyPenalties(session, steps)
	}

	if !session.Cheated && session.Score > 0 {
		_, err := db.Exec("INSERT INTO scores (name, score) VALUES (?, ?)", name, session.Score)
		if err == nil {
			log.Printf("[TELEMETRY_SUBMIT] token=%s name=%s score=%d fruits=%d ip=%s",
				token, name, session.Score, session.FruitsEaten, ip)

			log.Printf("[SCORE_SAVED] token=%s name='%s' score=%d fruits=%d ip=%s",
				token[:8], name, session.Score, session.FruitsEaten, ip)
		} else {
			log.Printf("[DB_ERROR] token=%s ip=%s err=%v", token, ip, err)
			http.Error(w, "Backend Error", http.StatusInternalServerError)
			return
		}
	} else {
		log.Printf("[SCORE_IGNORED] token=%s name='%s' ip=%s", token[:8], name, ip)
	}

	fmt.Fprint(w, "OK")
}

func handleScores(w http.ResponseWriter, r *http.Request) {
	if r.Header.Get("X-Client-Version") != RequiredClientVersion {
		fmt.Fprint(w, "UPDATE REQUIRED!| |0|0\n | |0|0\nPLEASE GET THE | |0|0\nLATEST VERSION | |0|0\n")
		return
	}
	limit, _ := strconv.Atoi(r.PathValue("limit"))
	limit = max(1, min(limit, 100))

	rows, err := db.Query("SELECT name, score FROM scores ORDER BY score DESC, timestamp ASC LIMIT ?", limit)
	if err != nil {
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

	port := os.Getenv("PORT")
	if port == "" {
		port = DefaultPort
	}

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
	mux.HandleFunc("POST /eat/{token}", handleEat)
	mux.HandleFunc("GET /cheat/{token}", handleCheat)
	mux.HandleFunc("GET /quit/{token}", handleQuit)
	mux.HandleFunc("GET /submit/{token}/{name}/{steps}", handleSubmit)
	mux.HandleFunc("GET /scores/{limit}", handleScores)

	handler := rateLimitMiddleware(mux)

	log.Printf("[Server] Starting secure backend on 0.0.0.0:%s", port)
	if err := http.ListenAndServe("0.0.0.0:"+port, handler); err != nil {
		log.Fatalf("Server crashed: %v", err)
	}
}
