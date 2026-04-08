package main

import (
	"crypto/rand"
	"database/sql"
	"encoding/hex"
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

// --- Configuration & Game Rules ---
const (
	Port                  = "8000"
	DBPath                = "scores.db"
	GameWidth             = 25
	GameHeight            = 20
	InitialDelay          = 250000.0
	SpeedupFactor         = 0.985
	PointsPerFruit        = 10
	CheatTimeout          = 5000
	InitialSize           = 1
	PenaltyInterval       = 10
	PenaltyAmount         = 1
	SpawnFruitMaxAttempts = 10000
	MaxScore              = GameWidth * GameHeight * PointsPerFruit
)

// --- Structures ---
type Session struct {
	Score       int
	FruitsEaten int
	LastPing    time.Time
	Cheated     bool
	Seed        uint32
	HeadX       int // Represents the true X position of the snake's head
	HeadY       int // Represents the true Y position of the snake's head
	LastSteps   int
}

// Global state
var (
	db             *sql.DB
	activeSessions = make(map[string]*Session)
	sessionMutex   sync.RWMutex
)

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
	if PenaltyInterval <= 0 {
		return
	}
	for step := session.LastSteps + 1; step <= newSteps; step++ {
		if step%PenaltyInterval == 0 {
			// Leveraging Go 1.21+ built-in max() function
			session.Score = max(0, session.Score-PenaltyAmount)
		}
	}
}

// flagCheater: Centralized cheating flag execution
func flagCheater(token string, reason string, logMsg string, ip string) {
	sessionMutex.Lock()
	defer sessionMutex.Unlock()
	if session, exists := activeSessions[token]; exists {
		session.Cheated = true
		log.Printf("[%s] CHEAT (%s): %s | %s", ip, reason, token[:8], logMsg)
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

// --- Database Management ---

func initDB() {
	var err error
	db, err = sql.Open("sqlite3", DBPath)
	if err != nil {
		log.Fatalf("Database opening failed: %v", err)
	}

	query := `CREATE TABLE IF NOT EXISTS scores (
		name TEXT, 
		score INTEGER, 
		timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
	)`
	if _, err = db.Exec(query); err != nil {
		log.Fatalf("Database table creation failed: %v", err)
	}
	log.Println("[Server] Database initialized successfully.")
}

// cleanupStaleSessions: Removes sessions inactive for more than 15 minutes.
// Executed securely via a background ticker to prevent DoS attacks on the mutex.
func cleanupStaleSessions() {
	sessionMutex.Lock()
	defer sessionMutex.Unlock()
	removed := 0
	for token, session := range activeSessions {
		// Leveraging Go 1.21+ time.Since for readability
		if time.Since(session.LastPing).Seconds() > 900 {
			delete(activeSessions, token)
			removed++
		}
	}
	if removed > 0 {
		log.Printf("[Server] CLEANUP: Removed %d abandoned sessions.", removed)
	}
}

// --- API Routes ---

func handleRules(w http.ResponseWriter, r *http.Request) {
	fmt.Fprintf(w, "%d|%d|%d|%f|%d|%d|%d|%d|%d|%d",
		GameWidth, GameHeight, int(InitialDelay), SpeedupFactor, PointsPerFruit,
		CheatTimeout, InitialSize, PenaltyInterval, PenaltyAmount, SpawnFruitMaxAttempts)
}

func handleToken(w http.ResponseWriter, r *http.Request) {
	// Generate 16-byte hex token
	bytes := make([]byte, 16)
	rand.Read(bytes)
	token := hex.EncodeToString(bytes)

	// Generate 31-bit seed
	seedBytes := make([]byte, 4)
	rand.Read(seedBytes)
	originalSeed := (uint32(seedBytes[0])<<24 | uint32(seedBytes[1])<<16 | uint32(seedBytes[2])<<8 | uint32(seedBytes[3])) & 0x7fffffff

	// Seed advancement logic (offset) required to maintain strict cryptographic sync with the C client
	currentSeed := originalSeed
	offsetX := 0
	if GameWidth%2 == 0 {
		currentSeed = lcgRand(currentSeed)
		offsetX = int(currentSeed % 2)
	}

	offsetY := 0
	if GameHeight%2 == 0 {
		currentSeed = lcgRand(currentSeed)
		offsetY = int(currentSeed % 2)
	}

	// Initialize session with the snake's exact starting position
	sessionMutex.Lock()
	activeSessions[token] = &Session{
		Score:       0,
		FruitsEaten: 0,
		LastPing:    time.Now(),
		Cheated:     false,
		Seed:        currentSeed,
		HeadX:       (GameWidth / 2) - offsetX,
		HeadY:       (GameHeight / 2) - offsetY,
		LastSteps:   0,
	}
	sessionMutex.Unlock()

	log.Printf("[%s] SESSION START: Token %s... | Seed: %d", getRemoteIP(r), token[:8], originalSeed)
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

	// 1. Fruit Validation (Implicit body collision bypass)
	validFruit := false
	tempSeed := session.Seed
	for i := 0; i < SpawnFruitMaxAttempts; i++ {
		tempSeed = lcgRand(tempSeed)
		candX := int((tempSeed >> 16) % GameWidth)
		tempSeed = lcgRand(tempSeed)
		candY := int((tempSeed >> 16) % GameHeight)

		if candX == fx && candY == fy {
			validFruit = true
			session.Seed = tempSeed // Sync seed to the accepted fruit
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

	// 2. Manhattan Distance (Calculated from current head position)
	manhattan := int(math.Abs(float64(fx-session.HeadX)) + math.Abs(float64(fy-session.HeadY)))
	if deltaSteps < manhattan {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] CHEAT (Movement): %s | %d steps taken for dist %d", ip, token[:8], deltaSteps, manhattan)
		http.Error(w, "Impossible move", http.StatusBadRequest)
		return
	}

	// 3. Time-based Anti-Spam
	currentDelaySec := (InitialDelay / 1000000.0) * math.Pow(SpeedupFactor, float64(session.FruitsEaten))
	minPingInterval := currentDelaySec * float64(deltaSteps) * 0.8 // 20% network margin

	if time.Since(session.LastPing).Seconds() < minPingInterval {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] CHEAT (Time): %s | Ping too fast for %d steps.", ip, token[:8], deltaSteps)
		http.Error(w, "Speedhack detected", http.StatusBadRequest)
		return
	}

	// 4. Update Score & Penalties
	applyPenalties(session, steps)
	session.Score += PointsPerFruit
	session.FruitsEaten++

	if session.Score >= MaxScore {
		session.Cheated = true
		sessionMutex.Unlock()
		log.Printf("[%s] CHEAT (Limit): %s | Score %d exceeded capacity.", ip, token[:8], session.Score)
		http.Error(w, "Score limit exceeded", http.StatusBadRequest)
		return
	}

	// 5. Update State
	session.LastPing = time.Now()
	session.HeadX = fx
	session.HeadY = fy
	session.LastSteps = steps
	sessionMutex.Unlock()

	fmt.Fprint(w, "Yum")
}

func handleCheat(w http.ResponseWriter, r *http.Request) {
	flagCheater(r.PathValue("token"), "Local", "Local detection triggered.", getRemoteIP(r))
	fmt.Fprint(w, "Flagged")
}

func handleSubmit(w http.ResponseWriter, r *http.Request) {
	token := r.PathValue("token")
	name := r.PathValue("name")
	steps, _ := strconv.Atoi(r.PathValue("steps"))
	ip := getRemoteIP(r)

	// Safely detach the session from the global map to prevent double-submits
	sessionMutex.Lock()
	session, exists := activeSessions[token]
	if exists {
		delete(activeSessions, token)
	}
	sessionMutex.Unlock()

	// Always return 200 OK for UI fluidity
	if !exists {
		log.Printf("[%s] SUBMIT REJECTED: Expired or invalid token %s", ip, token[:8])
		fmt.Fprint(w, "OK")
		return
	}

	// Strict XSS and format validation
	if len(name) == 0 || len(name) > 8 || !isAlphanumeric(name) {
		log.Printf("[%s] SUBMIT REJECTED: Invalid name '%s' from %s", ip, name, token[:8])
		fmt.Fprint(w, "OK")
		return
	}

	if session.Cheated {
		log.Printf("[%s] BANNED SUBMISSION: Player '%s' attempted to submit from flagged session. Forcing score to 0.", ip, name)
		session.Score = 0
	}

	// Apply final penalties
	if session.Score > 0 {
		applyPenalties(session, steps)
	}

	finalScore := session.Score
	if finalScore < 0 || finalScore > MaxScore {
		log.Printf("[%s] SUBMIT REJECTED: Impossible score %d for '%s'. Forcing score to 0.", ip, finalScore, name)
		finalScore = 0
	}

	// Save to DB
	if finalScore > 0 {
		_, err := db.Exec("INSERT INTO scores (name, score) VALUES (?, ?)", name, finalScore)
		if err != nil {
			log.Printf("[%s] Database error: %v", ip, err)
			http.Error(w, "Backend Error", http.StatusInternalServerError)
			return
		}
		log.Printf("[%s] SCORE SAVED: '%s' | Score: %d | Fruits: %d", ip, name, finalScore, session.FruitsEaten)
	} else {
		log.Printf("[%s] SCORE IGNORED (Zero or Cheated): '%s' submission processed but not saved to DB.", ip, name)
	}

	fmt.Fprint(w, "OK")
}

func handleScores(w http.ResponseWriter, r *http.Request) {
	limit, _ := strconv.Atoi(r.PathValue("limit"))

	// Bound the limit to prevent OOM (Out Of Memory) or excessive database queries
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
	if err := godotenv.Load(); err != nil {
		log.Println("[Warning] No .env file found or error reading it. Relying on system environment variables.")
	}

	port := os.Getenv("PORT")
	if port == "" {
		port = Port
	}

	initDB()
	defer db.Close()

	// Launch background task to securely clean stale sessions without blocking requests
	go func() {
		ticker := time.NewTicker(5 * time.Minute)
		defer ticker.Stop()
		for range ticker.C {
			cleanupStaleSessions()
		}
	}()

	// HTTP Routing
	http.HandleFunc("GET /rules", handleRules)
	http.HandleFunc("GET /token", handleToken)
	http.HandleFunc("GET /eat/{token}/{steps}/{fx}/{fy}", handleEat)
	http.HandleFunc("GET /cheat/{token}", handleCheat)
	http.HandleFunc("GET /submit/{token}/{name}/{steps}", handleSubmit)
	http.HandleFunc("GET /scores/{limit}", handleScores)

	log.Printf("[Server] Starting backend on 0.0.0.0:%s", port)
	if err := http.ListenAndServe("0.0.0.0:"+port, nil); err != nil {
		log.Fatalf("Server crashed: %v", err)
	}
}
