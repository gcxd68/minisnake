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
	RequiredClientVersion = "5"
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
	MinFruitDist          int
	MaxScore              int `json:"-"`
}

type Point struct {
	X int
	Y int
}

type Session struct {
	Score         int
	FruitsEaten   int
	StartTime     time.Time
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

	// ANTI-LAG FIELDS
	RecentActualTime   []float64 // Sliding window for actual ping delays
	RecentExpectedTime []float64 // Sliding window for expected delays
	VacatedTailX       int       // Old tail position to exclude from spawn
	VacatedTailY       int       // Old tail position to exclude from spawn

	// HEAVY PHYSICS FIELDS (Optimized with O(1) Ring Buffer)
	ExpectedSeq int
	Grid        []bool  // O(1) Spatial Hashing grid
	Grow        int     // Pending growth counter
	Body        []Point // Fixed-size pre-allocated array for the snake body
	HeadIdx     int     // Ring buffer pointer to the head
	TailIdx     int     // Ring buffer pointer to the tail
	Size        int     // Current active length of the snake
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
		MinFruitDist:          2,
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
// OPTIMIZATION: Uses O(1) boolean Grid for instant collision checks.
// LATENCY-AWARE: Incorporates the 'latencySteps' prediction cloud to strictly prevent ghost-eating.
func spawnFruit(session *Session, prevX, prevY, vacatedTailX, vacatedTailY, latencySteps int) {
	// 1. Cap the latency steps to prevent impossible mathematical constraints during huge lag spikes.
	// A max radius of 4 tiles (Manhattan) covers up to ~400ms of latency, which is more than enough.
	if latencySteps > 4 {
		latencySteps = 4
	}

	idealDist := Rules.MinFruitDist + latencySteps

	for i := 0; i < Rules.SpawnFruitMaxAttempts; i++ {
		session.Seed = lcgRand(session.Seed)
		candX := int((session.Seed >> 16) % uint32(Rules.GameWidth))
		session.Seed = lcgRand(session.Seed)
		candY := int((session.Seed >> 16) % uint32(Rules.GameHeight))

		gridIndex := candY*Rules.GameWidth + candX

		// 2. Stepped Graceful Degradation (Multi-stage fallback)
		// Instead of dropping latency protection instantly to zero, we lower the constraints gradually
		// if the RNG struggles to find a valid spot on a crowded board.
		currentMinDist := idealDist

		if i > 300 {
			// Panic mode: Board is extremely full, accept any empty tile
			currentMinDist = 0
		} else if i > 200 {
			// Severe fallback: Accept tiles very close to the head
			currentMinDist = 1
		} else if i > 100 {
			// Moderate fallback: Drop latency protection, but maintain base anti-lag distance
			currentMinDist = Rules.MinFruitDist
		}

		dist := int(math.Abs(float64(candX-session.HeadX)) + math.Abs(float64(candY-session.HeadY)))
		isVacatedTail := (candX == vacatedTailX && candY == vacatedTailY)

		// Ensure the fruit is NOT on the snake's body, NOT on the previous fruit,
		// NOT on the just-vacated tail tile, AND satisfies the current distance tier.
		if !session.Grid[gridIndex] && (candX != prevX || candY != prevY) && !isVacatedTail && dist >= currentMinDist {
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
		log.Printf("[%s] [%s...] [SHADOWBAN_FINGERPRINT]", ip, token[:8])
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

	// Generate secure token
	bytes := make([]byte, 16)
	rand.Read(bytes)
	token := hex.EncodeToString(bytes)

	// Create the global server seed for future fruits
	seedBytes := make([]byte, 4)
	rand.Read(seedBytes)
	currentSeed := (uint32(seedBytes[0])<<24 | uint32(seedBytes[1])<<16 | uint32(seedBytes[2])<<8 | uint32(seedBytes[3])) & 0x7fffffff

	// Initialize client seed with the randomly generated core seed
	// to ensure the grid offset is truly random for each session.
	clientSeed := currentSeed
	offsetX, offsetY := 0, 0

	if Rules.GameWidth%2 == 0 {
		clientSeed = lcgRand(clientSeed)
		offsetX = int(clientSeed % 2)
	}
	if Rules.GameHeight%2 == 0 {
		clientSeed = lcgRand(clientSeed)
		offsetY = int(clientSeed % 2)
	}

	headX := (Rules.GameWidth / 2) - offsetX
	headY := (Rules.GameHeight / 2) - offsetY
	head := Point{X: headX, Y: headY}

	// Initialize the O(1) tracking grid and Ring Buffer
	grid := make([]bool, Rules.GameWidth*Rules.GameHeight)
	grid[headY*Rules.GameWidth+headX] = true

	// Expanded Ring Buffer capacity to 10,000 to match the C client natively.
	// This prevents memory wrap-around overwrites when the snake approaches the maximum board size.
	bodyRing := make([]Point, 10000)
	bodyRing[0] = head

	now := time.Now()
	session := &Session{
		Score:              0,
		FruitsEaten:        0,
		StartTime:          now,
		LastPing:           now,
		Cheated:            false,
		Seed:               currentSeed,
		HeadX:              headX,
		HeadY:              headY,
		LastSteps:          0,
		PerfectPaths:       0,
		TotalSteps:         0,
		RecentDetours:      make([]int, 0, 30),
		RecentActualTime:   make([]float64, 0, 5),
		RecentExpectedTime: make([]float64, 0, 5),
		VacatedTailX:       -1,
		VacatedTailY:       -1,
		ExpectedSeq:        1,
		Grid:               grid,
		Grow:               Rules.InitialSize - 1,
		Body:               bodyRing,
		HeadIdx:            0,
		TailIdx:            0,
		Size:               1,
	}

	// Initial spawn has no latency cloud
	spawnFruit(session, -1, -1, -1, -1, 0)

	sessionMutex.Lock()
	activeSessions[token] = session
	sessionMutex.Unlock()

	log.Printf("[%s] [%s...] [SESSION_START]", ip, token[:8])
	fmt.Fprintf(w, "%s|%d|%d", token, session.TargetFruit.X, session.TargetFruit.Y)
}

func handleEat(w http.ResponseWriter, r *http.Request) {
	token := r.PathValue("token")
	ip := getRemoteIP(r)

	// 1. Decode the JSON POST Payload (with Memory Exhaustion DoS protection)
	r.Body = http.MaxBytesReader(w, r.Body, 1024*10)
	var payload EatPayload
	if err := json.NewDecoder(r.Body).Decode(&payload); err != nil {
		http.Error(w, "Bad Request", http.StatusBadRequest)
		return
	}

	sessionMutex.Lock()
	session, exists := activeSessions[token]

	// Helper: Generates a believable fake fruit WITHOUT mutating the real session state.
	// This prevents critical data races if called outside of the sessionMutex.
	sendFakeFruit := func() {
		fakeSeed := session.Seed
		fakeSeed = lcgRand(fakeSeed)
		fakeX := int((fakeSeed >> 16) % uint32(Rules.GameWidth))
		fakeSeed = lcgRand(fakeSeed)
		fakeY := int((fakeSeed >> 16) % uint32(Rules.GameHeight))
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

	// 3. HEAVY PHYSICS: O(1) Simulation using Spatial Hashing & Ring Buffers
	// Safely replay the client's path instantly without O(N) array shifting CPU penalties
	validMoves := 0
	vacatedTailValid := false
	var currentVacatedTail Point

	for _, move := range payload.Path {
		if session.Size == 0 {
			break
		}

		newHead := session.Body[session.HeadIdx]
		switch move {
		case 'U':
			newHead.Y--
		case 'D':
			newHead.Y++
		case 'L':
			newHead.X--
		case 'R':
			newHead.X++
		case ' ', 'X':
			continue // Ignore STOP instructions safely
		default:
			continue
		}

		validMoves++

		// A. Wall Collision Check
		if newHead.X < 0 || newHead.X >= Rules.GameWidth || newHead.Y < 0 || newHead.Y >= Rules.GameHeight {
			session.Cheated = true
			log.Printf("[%s] [%s...] [SHADOWBAN_PHYSICS] Wall collision at (%d,%d)", ip, token[:8], newHead.X, newHead.Y)
			break
		}

		// B. Self Collision Check O(1)
		gridIndex := newHead.Y*Rules.GameWidth + newHead.X
		tail := session.Body[session.TailIdx]

		// Exception: It's valid if the snake's head enters the space the tail is currently leaving
		isTailMoving := (newHead.X == tail.X && newHead.Y == tail.Y && session.Grow == 0)

		if session.Grid[gridIndex] && !isTailMoving {
			session.Cheated = true
			log.Printf("[%s] [%s...] [SHADOWBAN_PHYSICS] Self-collision at (%d,%d)", ip, token[:8], newHead.X, newHead.Y)
			break
		}

		// C. Mutate State: Advance Tail or Grow (MUST BE FIRST TO PREVENT FALSE GRID OVERRIDES)
		if session.Grow > 0 {
			session.Grow--
			session.Size++
		} else {
			oldTail := session.Body[session.TailIdx]
			currentVacatedTail = oldTail
			vacatedTailValid = true

			// Remove tail from spatial grid before head claims the space
			session.Grid[oldTail.Y*Rules.GameWidth+oldTail.X] = false

			// Advance tail pointer in the ring buffer
			session.TailIdx = (session.TailIdx + 1) % len(session.Body)
		}

		// D. Mutate State: Advance Head (SECOND)
		session.HeadIdx = (session.HeadIdx + 1) % len(session.Body)
		session.Body[session.HeadIdx] = newHead
		session.Grid[gridIndex] = true
	}

	// Capture the last vacated tail position to prevent immediate fruit spawns on it
	// If the tail did not move during this sequence, clear the legacy state.
	if vacatedTailValid {
		session.VacatedTailX = currentVacatedTail.X
		session.VacatedTailY = currentVacatedTail.Y
	} else {
		session.VacatedTailX = -1
		session.VacatedTailY = -1
	}

	if !session.Cheated && validMoves != deltaSteps {
		session.Cheated = true
		log.Printf("[%s] [%s...] [SHADOWBAN_PHYSICS] move_count_mismatch expected=%d got=%d", ip, token[:8], deltaSteps, validMoves)
	}

	// 4. Target Validation
	if !session.Cheated {
		currentHead := session.Body[session.HeadIdx]
		if session.Size == 0 || currentHead.X != payload.Fx || currentHead.Y != payload.Fy || payload.Fx != session.TargetFruit.X || payload.Fy != session.TargetFruit.Y {
			session.Cheated = true
			log.Printf("[%s] [%s...] [SHADOWBAN_TARGET] expected=(%d,%d) got=(%d,%d)", ip, token[:8], session.TargetFruit.X, session.TargetFruit.Y, payload.Fx, payload.Fy)
		}
	}

	// 5. Local Time Corridor (Sliding Window Lag Compensation)
	currentDelaySec := (Rules.InitialDelay / 1000000.0) * math.Pow(Rules.SpeedupFactor, float64(session.FruitsEaten))
	expectedTime := currentDelaySec * float64(deltaSteps)
	actualTime := time.Since(session.LastPing).Seconds()

	// Prevent the initial idle time (Start Screen) from poisoning the sliding window.
	// By forcing the first ping to match the expected time, we safely absorb the wait.
	if session.FruitsEaten == 0 {
		actualTime = expectedTime
	}

	// Append metrics to the sliding window
	session.RecentActualTime = append(session.RecentActualTime, actualTime)
	session.RecentExpectedTime = append(session.RecentExpectedTime, expectedTime)

	// Keep only the last 5 data points
	if len(session.RecentActualTime) > 5 {
		session.RecentActualTime = session.RecentActualTime[1:]
		session.RecentExpectedTime = session.RecentExpectedTime[1:]
	}

	// Sum the window to absorb network jitter & lag spikes safely
	sumExpected := 0.0
	sumActual := 0.0
	for i := range session.RecentActualTime {
		sumExpected += session.RecentExpectedTime[i]
		sumActual += session.RecentActualTime[i]
	}

	// Validate against the smoothed total time
	upperBound := sumExpected + (float64(Rules.CheatTimeout) / 1000.0) + 5.0

	if sumActual < math.Max(0, (sumExpected*0.75)-1.0) || sumActual > upperBound {
		session.Cheated = true
		log.Printf("[%s] [%s...] [SHADOWBAN_TIME] smoothed_expected=%.1f smoothed_actual=%.1f (window=%d)", ip, token[:8], sumExpected, sumActual, len(session.RecentActualTime))
	}

	if session.Cheated {
		sessionMutex.Unlock()
		sendFakeFruit()
		return
	}

	// 6. Behavioral Analytics & Active Turing Tests
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

	// Activate Turing tests after enough data points (30 fruits)
	if currentFruits >= 30 {
		// A. Manhattan Filter (Active Ban for strictly perfect routing)
		perfRatio := (session.PerfectPaths * 100) / currentFruits
		if perfRatio >= 95 {
			session.Cheated = true
			log.Printf("[%s] [%s...] [SHADOWBAN_BEHAVIOR] perfect_ratio=%d", ip, token[:8], perfRatio)
			sessionMutex.Unlock()
			sendFakeFruit()
			return
		}

		// B. Variance Filter (Active Ban for highly regular/robotic detours)
		if len(session.RecentDetours) == 30 {
			sum := 0.0
			for _, v := range session.RecentDetours {
				sum += float64(v)
			}
			mean := sum / 30.0

			variance := 0.0
			for _, v := range session.RecentDetours {
				variance += math.Pow(float64(v)-mean, 2)
			}
			variance /= 30.0

			// Trigger a shadowban if the detour variance is inhumanly consistent
			if variance < 1.0 {
				session.Cheated = true
				log.Printf("[%s] [%s...] [SHADOWBAN_VARIANCE] mean=%.2f variance=%.2f (Too robotic)", ip, token[:8], mean, variance)
				sessionMutex.Unlock()
				sendFakeFruit()
				return
			}
		}
	}

	// Apply valid game mutations
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

	// 7. Latency-Aware Spawning (Probability Cloud)
	// Calculate the actual network ping (time elapsed beyond the physical travel time)
	ping := math.Max(0.0, actualTime-expectedTime)
	// Extrapolate how many extra tiles the client could have traveled locally during this ping
	latencySteps := int(math.Ceil(ping / currentDelaySec))

	// Wrap up and generate next target, gracefully skipping the recently vacated tail tile
	// AND enforcing the latency probability cloud
	session.LastPing = time.Now()
	session.HeadX, session.HeadY, session.LastSteps = payload.Fx, payload.Fy, payload.Steps
	session.ExpectedSeq++

	spawnFruit(session, payload.Fx, payload.Fy, session.VacatedTailX, session.VacatedTailY, latencySteps)

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
	ip := getRemoteIP(r)

	sessionMutex.Lock()
	session, exists := activeSessions[token]
	if exists {
		delete(activeSessions, token)
	}
	sessionMutex.Unlock()

	// If the session existed, log the aborted game in a unified way
	if exists {
		log.Printf("[%s] [%s...] [SCORE_IGNORED] reason=no_name score=%d fruits=%d",
			ip, token[:8], session.Score, session.FruitsEaten)
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

	// ACCURATE INTEGRAL CALCULATION:
	// To find the absolute mathematical minimum time for the entire game,
	// we assume the player spent the minimum steps possible (MinFruitDist)
	// during the early (slower) levels, and took all extra wandering steps at their final (fastest) level.
	baseDelay := Rules.InitialDelay / 1000000.0
	minExpected := 0.0

	minStepsPerFruit := Rules.MinFruitDist
	extraSteps := steps - (session.FruitsEaten * minStepsPerFruit)
	if extraSteps < 0 {
		extraSteps = 0 // Failsafe
	}

	// 1. Time spent eating fruits as fast as physically possible
	for i := 0; i < session.FruitsEaten; i++ {
		delayAtLevel := baseDelay * math.Pow(Rules.SpeedupFactor, float64(i))
		minExpected += float64(minStepsPerFruit) * delayAtLevel
	}

	// 2. Time spent wandering at the maximum speed achieved
	finalDelay := baseDelay * math.Pow(Rules.SpeedupFactor, float64(session.FruitsEaten))
	minExpected += float64(extraSteps) * finalDelay

	// Apply a strict 15% tolerance margin for network jitter and server clock precision
	minExpected = minExpected * 0.85

	if steps > 0 && duration < minExpected {
		session.Cheated = true
		log.Printf("[%s] [%s...] [SHADOWBAN_GLOBALSPEED] duration=%.1f steps=%d min_expected=%.1f", ip, token[:8], duration, steps, minExpected)
	}

	// Apply penalties to ALL players (including shadowbanned ones)
	// to ensure the final telemetry score perfectly mirrors a realistic game.
	if session.Score > 0 {
		applyPenalties(session, steps)
	}

	if !session.Cheated && session.Score > 0 {
		_, err := db.Exec("INSERT INTO scores (name, score) VALUES (?, ?)", name, session.Score)
		if err == nil {
			log.Printf("[%s] [%s...] [SCORE_SAVED] name='%s' score=%d fruits=%d", ip, token[:8], name, session.Score, session.FruitsEaten)
		} else {
			log.Printf("[%s] [%s...] [DB_ERROR] err=%v", ip, token[:8], err)
			http.Error(w, "Backend Error", http.StatusInternalServerError)
			return
		}
	} else {
		if session.Cheated {
			// Log the exact simulated score and fruit count for shadowbanned bots to gather accurate telemetry
			log.Printf("[%s] [%s...] [SCORE_IGNORED] reason=shadowbanned name='%s' score=%d fruits=%d", ip, token[:8], name, session.Score, session.FruitsEaten)
		} else {
			// Standard minimalist log for legitimate players who simply scored 0
			log.Printf("[%s] [%s...] [SCORE_IGNORED] reason=0_score name='%s'", ip, token[:8], name)
		}
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
