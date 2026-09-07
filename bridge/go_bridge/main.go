package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	_ "github.com/mattn/go-sqlite3"

	waProto "go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/store/sqlstore"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
	waLog "go.mau.fi/whatsmeow/util/log"

	"github.com/mdp/qrterminal/v3"
)

// ============================================================
// Configuration
// ============================================================

const (
	httpPort  = 8080
	bridgeDB  = "bridge.db"
	sessionDB = "whatsapp.db"
)

// ============================================================
// Data structures
// ============================================================

type Chat struct {
	ID       string `json:"id"`
	Name     string `json:"name"`
	Unread   int    `json:"unread"`
	IsGroup  bool   `json:"isGroup"`
	LastTime int64  `json:"lastTime"`
}

type Message struct {
	ID        string `json:"id"`
	ChatID    string `json:"chatId"`
	Sender    string `json:"sender"`
	Text      string `json:"text"`
	IsMe      bool   `json:"isMe"`
	Timestamp int64  `json:"timestamp"`
	Type      string `json:"type"`
}

type SendMessageRequest struct {
	ChatID string `json:"chatId"`
	Text   string `json:"text"`
}

type Server struct {
	client *whatsmeow.Client
	db     *sql.DB

	mu    sync.RWMutex
	ready bool
}

// ============================================================
// Main
// ============================================================

func main() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// --------------------------------------------------------
	// Open bridge database
	// --------------------------------------------------------

	db, err := sql.Open(
		"sqlite3",
		bridgeDB+"?_foreign_keys=on",
	)
	if err != nil {
		log.Fatal("Failed to open bridge database:", err)
	}

	defer db.Close()

	if err := initDatabase(db); err != nil {
		log.Fatal("Failed to initialize bridge database:", err)
	}

	// --------------------------------------------------------
	// Open WhatsApp session database
	// --------------------------------------------------------

	dbLog := waLog.Stdout("Database", "WARN", true)

	container, err := sqlstore.New(
		ctx,
		"sqlite3",
		sessionDB+"?_foreign_keys=on",
		dbLog,
	)
	if err != nil {
		log.Fatal("Failed to open WhatsApp session database:", err)
	}

	defer container.Close()

	deviceStore, err := container.GetFirstDevice(ctx)
	if err != nil {
		log.Fatal("Failed to get WhatsApp device:", err)
	}

	// --------------------------------------------------------
	// Create WhatsApp client
	// --------------------------------------------------------

	clientLog := waLog.Stdout("WhatsApp", "WARN", true)

	client := whatsmeow.NewClient(
		deviceStore,
		clientLog,
	)

	server := &Server{
		client: client,
		db:     db,
	}

	// --------------------------------------------------------
	// Register WhatsApp event handler
	// --------------------------------------------------------

	client.AddEventHandler(server.handleEvent)

	// --------------------------------------------------------
	// Start HTTP server
	// --------------------------------------------------------

	mux := http.NewServeMux()

	mux.HandleFunc("/api/status", server.handleStatus)
	mux.HandleFunc("/api/chats", server.handleChats)

	mux.HandleFunc("/api/messages", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodGet:
			server.handleMessages(w, r)

		case http.MethodPost:
			server.handleSendMessage(w, r)

		default:
			w.Header().Set("Allow", "GET, POST")

			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{
				"error": "Method not allowed",
			})
		}
	})

	httpServer := &http.Server{
		Addr:    ":" + strconv.Itoa(httpPort),
		Handler: loggingMiddleware(mux),
	}

	go func() {
		log.Printf(
			"Bridge server listening on http://localhost:%d",
			httpPort,
		)

		if err := httpServer.ListenAndServe(); err != nil &&
			err != http.ErrServerClosed {
			log.Fatal("HTTP server:", err)
		}
	}()

	// --------------------------------------------------------
	// Connect to WhatsApp
	// --------------------------------------------------------

	if client.Store.ID == nil {
		log.Println("No WhatsApp session found.")
		log.Println("Starting QR login...")

		qrChan, err := client.GetQRChannel(ctx)
		if err != nil {
			log.Fatal("Failed to create QR channel:", err)
		}

		if err := client.Connect(); err != nil {
			log.Fatal("Failed to connect to WhatsApp:", err)
		}

		for evt := range qrChan {
			switch evt.Event {

			case "code":
				fmt.Println()
				fmt.Println("Scan this QR code with WhatsApp:")
				fmt.Println()

				// GenerateHalfBlock does not return an error.
				qrterminal.GenerateHalfBlock(
					evt.Code,
					qrterminal.L,
					os.Stdout,
				)

				fmt.Println()

			default:
				log.Println(
					"WhatsApp login event:",
					evt.Event,
				)
			}
		}
	} else {
		log.Println("Existing WhatsApp session found.")

		if err := client.Connect(); err != nil {
			log.Fatal("Failed to connect to WhatsApp:", err)
		}
	}

	// Connected event normally sets this.
	// This also acts as a fallback.
	server.setReady(true)

	log.Println("WhatsApp Client is ready!")

	// --------------------------------------------------------
	// Wait for shutdown
	// --------------------------------------------------------

	sigChan := make(chan os.Signal, 1)

	signal.Notify(
		sigChan,
		os.Interrupt,
		syscall.SIGTERM,
	)

	<-sigChan

	log.Println("Shutting down...")

	server.setReady(false)

	client.Disconnect()

	shutdownCtx, shutdownCancel :=
		context.WithTimeout(
			context.Background(),
			5*time.Second,
		)
	defer shutdownCancel()

	if err := httpServer.Shutdown(shutdownCtx); err != nil {
		log.Println("HTTP shutdown error:", err)
	}

	log.Println("Bridge stopped.")
}

// ============================================================
// Database
// ============================================================

func initDatabase(db *sql.DB) error {
	_, err := db.Exec(`
		CREATE TABLE IF NOT EXISTS chats (
			id TEXT PRIMARY KEY,
			name TEXT NOT NULL DEFAULT '',
			unread INTEGER NOT NULL DEFAULT 0,
			is_group INTEGER NOT NULL DEFAULT 0,
			last_time INTEGER NOT NULL DEFAULT 0
		);

		CREATE TABLE IF NOT EXISTS messages (
			id TEXT PRIMARY KEY,
			chat_id TEXT NOT NULL,
			sender TEXT NOT NULL DEFAULT '',
			text TEXT NOT NULL DEFAULT '',
			is_me INTEGER NOT NULL DEFAULT 0,
			timestamp INTEGER NOT NULL DEFAULT 0,
			type TEXT NOT NULL DEFAULT ''
		);

		CREATE INDEX IF NOT EXISTS idx_messages_chat
		ON messages(chat_id, timestamp);

		CREATE INDEX IF NOT EXISTS idx_chats_time
		ON chats(last_time DESC);
	`)

	return err
}

// ============================================================
// WhatsApp event handling
// ============================================================

func (s *Server) handleEvent(raw interface{}) {
	switch evt := raw.(type) {

	case *events.Message:
		s.handleIncomingMessage(evt)

	case *events.Connected:
		log.Println("WhatsApp connected.")
		s.setReady(true)

	case *events.Disconnected:
		log.Println("WhatsApp disconnected.")
		s.setReady(false)
	}
}

func (s *Server) handleIncomingMessage(evt *events.Message) {
	if evt == nil || evt.Info.ID == "" {
		return
	}

	info := evt.Info

	chatID := info.Chat.String()

	sender := info.PushName

	if sender == "" {
		sender = info.Sender.String()
	}

	isMe := info.IsFromMe

	text := extractMessageText(evt)

	messageType := "text"

	if text == "" {
		messageType = "other"
		text = "[Media]"
	}

	timestamp := info.Timestamp.Unix()

	isGroup := info.Chat.Server == types.GroupServer

	// --------------------------------------------------------
	// Save/update chat
	// --------------------------------------------------------

	chatName := sender

	if isGroup {
		chatName = chatID
	}

	_, err := s.db.Exec(`
		INSERT INTO chats (
			id,
			name,
			unread,
			is_group,
			last_time
		)
		VALUES (?, ?, ?, ?, ?)

		ON CONFLICT(id) DO UPDATE SET
			name = CASE
				WHEN excluded.name != ''
				THEN excluded.name
				ELSE chats.name
			END,
			unread = chats.unread + excluded.unread,
			last_time = excluded.last_time
	`,
		chatID,
		chatName,
		boolInt(!isMe),
		boolInt(isGroup),
		timestamp,
	)

	if err != nil {
		log.Println("Failed to save chat:", err)
	}

	// --------------------------------------------------------
	// Save message
	// --------------------------------------------------------

	_, err = s.db.Exec(`
		INSERT OR IGNORE INTO messages (
			id,
			chat_id,
			sender,
			text,
			is_me,
			timestamp,
			type
		)
		VALUES (?, ?, ?, ?, ?, ?, ?)
	`,
		string(info.ID),
		chatID,
		sender,
		text,
		boolInt(isMe),
		timestamp,
		messageType,
	)

	if err != nil {
		log.Println("Failed to save message:", err)
	}

	log.Printf(
		"Message: chat=%s sender=%s text=%q",
		chatID,
		sender,
		text,
	)
}

// ============================================================
// Message extraction
// ============================================================

func extractMessageText(evt *events.Message) string {
	if evt == nil || evt.Message == nil {
		return ""
	}

	msg := evt.Message

	// Normal text
	if conversation := msg.GetConversation(); conversation != "" {
		return conversation
	}

	// Extended text
	if extended := msg.GetExtendedTextMessage(); extended != nil {
		return extended.GetText()
	}

	// Image
	if image := msg.GetImageMessage(); image != nil {
		if image.GetCaption() != "" {
			return image.GetCaption()
		}

		return "[Image]"
	}

	// Video
	if video := msg.GetVideoMessage(); video != nil {
		if video.GetCaption() != "" {
			return video.GetCaption()
		}

		return "[Video]"
	}

	// Audio
	if msg.GetAudioMessage() != nil {
		return "[Audio]"
	}

	// Document
	if document := msg.GetDocumentMessage(); document != nil {
		if document.GetFileName() != "" {
			return "[Document] " + document.GetFileName()
		}

		return "[Document]"
	}

	// Sticker
	if msg.GetStickerMessage() != nil {
		return "[Sticker]"
	}

	// Location
	if msg.GetLocationMessage() != nil {
		return "[Location]"
	}

	// Contact
	if msg.GetContactMessage() != nil {
		return "[Contact]"
	}

	return ""
}

// ============================================================
// GET /api/status
// ============================================================

func (s *Server) handleStatus(
	w http.ResponseWriter,
	r *http.Request,
) {
	s.mu.RLock()
	ready := s.ready
	s.mu.RUnlock()

	status := "not_ready"

	if ready {
		status = "ready"
	}

	writeJSON(w, http.StatusOK, map[string]string{
		"whatsapp": status,
	})
}

// ============================================================
// GET /api/chats
// ============================================================

func (s *Server) handleChats(
	w http.ResponseWriter,
	r *http.Request,
) {
	if !s.isReady() {
		writeJSON(
			w,
			http.StatusServiceUnavailable,
			map[string]string{
				"error": "WhatsApp client is not ready yet.",
			},
		)
		return
	}

	rows, err := s.db.Query(`
		SELECT
			id,
			name,
			unread,
			is_group,
			last_time
		FROM chats
		ORDER BY last_time DESC
		LIMIT 20
	`)

	if err != nil {
		writeError(w, err)
		return
	}

	defer rows.Close()

	chats := make([]Chat, 0)

	for rows.Next() {
		var c Chat
		var isGroup int

		if err := rows.Scan(
			&c.ID,
			&c.Name,
			&c.Unread,
			&isGroup,
			&c.LastTime,
		); err != nil {
			writeError(w, err)
			return
		}

		c.IsGroup = isGroup != 0

		if c.Name == "" {
			c.Name = c.ID
		}

		chats = append(chats, c)
	}

	if err := rows.Err(); err != nil {
		writeError(w, err)
		return
	}

	writeJSON(w, http.StatusOK, chats)
}

// ============================================================
// GET /api/messages?chatId=...
// ============================================================

func (s *Server) handleMessages(
	w http.ResponseWriter,
	r *http.Request,
) {
	if !s.isReady() {
		writeJSON(
			w,
			http.StatusServiceUnavailable,
			map[string]string{
				"error": "WhatsApp client is not ready yet.",
			},
		)
		return
	}

	chatID := strings.TrimSpace(
		r.URL.Query().Get("chatId"),
	)

	if chatID == "" {
		writeJSON(
			w,
			http.StatusBadRequest,
			map[string]string{
				"error": "chatId is required",
			},
		)
		return
	}

	rows, err := s.db.Query(`
		SELECT
			id,
			chat_id,
			sender,
			text,
			is_me,
			timestamp,
			type
		FROM messages
		WHERE chat_id = ?
		ORDER BY timestamp DESC
		LIMIT 20
	`, chatID)

	if err != nil {
		writeError(w, err)
		return
	}

	defer rows.Close()

	messages := make([]Message, 0)

	for rows.Next() {
		var m Message
		var isMe int

		if err := rows.Scan(
			&m.ID,
			&m.ChatID,
			&m.Sender,
			&m.Text,
			&isMe,
			&m.Timestamp,
			&m.Type,
		); err != nil {
			writeError(w, err)
			return
		}

		m.IsMe = isMe != 0

		messages = append(messages, m)
	}

	if err := rows.Err(); err != nil {
		writeError(w, err)
		return
	}

	// Newest → oldest from SQL.
	// Reverse to oldest → newest.
	for i, j := 0, len(messages)-1; i < j; i, j = i+1, j-1 {
		messages[i], messages[j] =
			messages[j], messages[i]
	}

	// Opening the chat clears unread count.
	_, _ = s.db.Exec(`
		UPDATE chats
		SET unread = 0
		WHERE id = ?
	`, chatID)

	writeJSON(w, http.StatusOK, messages)
}

// ============================================================
// POST /api/messages
// ============================================================

func (s *Server) handleSendMessage(
	w http.ResponseWriter,
	r *http.Request,
) {
	if !s.isReady() {
		writeJSON(
			w,
			http.StatusServiceUnavailable,
			map[string]string{
				"error": "WhatsApp client is not ready yet.",
			},
		)
		return
	}

	var request SendMessageRequest

	if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
		writeJSON(
			w,
			http.StatusBadRequest,
			map[string]string{
				"error": "Invalid JSON body",
			},
		)
		return
	}

	request.ChatID = strings.TrimSpace(request.ChatID)

	if request.ChatID == "" {
		writeJSON(
			w,
			http.StatusBadRequest,
			map[string]string{
				"error": "chatId is required",
			},
		)
		return
	}

	if request.Text == "" {
		writeJSON(
			w,
			http.StatusBadRequest,
			map[string]string{
				"error": "text is required",
			},
		)
		return
	}

	// --------------------------------------------------------
	// Parse WhatsApp JID
	// --------------------------------------------------------

	jid, err := types.ParseJID(request.ChatID)

	if err != nil {
		writeJSON(
			w,
			http.StatusBadRequest,
			map[string]string{
				"error": "Invalid WhatsApp chat ID: " + err.Error(),
			},
		)
		return
	}

	// --------------------------------------------------------
	// Send message
	// --------------------------------------------------------

	msgID, err := s.client.SendMessage(
		context.Background(),
		jid,
		&waProto.Message{
			Conversation: &request.Text,
		},
	)

	if err != nil {
		writeError(w, err)
		return
	}

	// --------------------------------------------------------
	// Cache outgoing message
	// --------------------------------------------------------

	timestamp := time.Now().Unix()

	_, err = s.db.Exec(`
		INSERT OR IGNORE INTO messages (
			id,
			chat_id,
			sender,
			text,
			is_me,
			timestamp,
			type
		)
		VALUES (?, ?, ?, ?, 1, ?, 'text')
	`,
		string(msgID.ID),
		request.ChatID,
		"You",
		request.Text,
		timestamp,
	)

	if err != nil {
		log.Println(
			"Warning: failed to cache outgoing message:",
			err,
		)
	}

	// --------------------------------------------------------
	// Update chat
	// --------------------------------------------------------

	_, _ = s.db.Exec(`
		INSERT INTO chats (
			id,
			name,
			unread,
			is_group,
			last_time
		)
		VALUES (?, ?, 0, ?, ?)

		ON CONFLICT(id) DO UPDATE SET
			last_time = excluded.last_time
	`,
		request.ChatID,
		request.ChatID,
		boolInt(jid.Server == types.GroupServer),
		timestamp,
	)

	writeJSON(
		w,
		http.StatusOK,
		map[string]interface{}{
			"success":   true,
			"messageId": string(msgID.ID),
		},
	)
}

// ============================================================
// HTTP helpers
// ============================================================

func writeJSON(
	w http.ResponseWriter,
	status int,
	value interface{},
) {
	w.Header().Set(
		"Content-Type",
		"application/json",
	)

	w.WriteHeader(status)

	if err := json.NewEncoder(w).Encode(value); err != nil {
		log.Println("JSON response error:", err)
	}
}

func writeError(
	w http.ResponseWriter,
	err error,
) {
	log.Println("HTTP error:", err)

	writeJSON(
		w,
		http.StatusInternalServerError,
		map[string]string{
			"error": err.Error(),
		},
	)
}

func loggingMiddleware(
	next http.Handler,
) http.Handler {
	return http.HandlerFunc(
		func(
			w http.ResponseWriter,
			r *http.Request,
		) {
			start := time.Now()

			next.ServeHTTP(w, r)

			log.Printf(
				"%s %s (%s)",
				r.Method,
				r.URL.Path,
				time.Since(start),
			)
		},
	)
}

// ============================================================
// State helpers
// ============================================================

func (s *Server) setReady(value bool) {
	s.mu.Lock()
	s.ready = value
	s.mu.Unlock()
}

func (s *Server) isReady() bool {
	s.mu.RLock()
	defer s.mu.RUnlock()

	return s.ready
}

func boolInt(value bool) int {
	if value {
		return 1
	}

	return 0
}
