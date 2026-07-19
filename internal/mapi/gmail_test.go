package mapi

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

// GOTEST-01: HTTP-level tests for GmailClient.CreateDraft.
//
// Uses httptest.Server via the FOUND-03 NewGmailClientWithBase injection
// point so the real Gmail endpoint is never touched. Covers happy path,
// authentication failure, server-side error, network failure, and
// response-body parse error.

// newTestMail returns a minimal MailMessage that BuildFullMIME can encode
// without touching the filesystem (no attachments).
func newTestMail() *MailMessage {
	return &MailMessage{
		Version:    1,
		Timestamp:  "2026-04-10T00:00:00Z",
		Subject:    "Gmail client test",
		Body:       "body text",
		BodyFormat: "plain",
		Recipients: Recipients{
			To: []Recipient{{Name: "Alice", Address: "alice@example.com"}},
		},
	}
}

func TestGmailClient_CreateDraft(t *testing.T) {
	type stubHandler struct {
		status int
		body   string
	}

	cases := []struct {
		name         string
		stub         stubHandler
		closeServer  bool // true = start then close, simulating network failure
		wantID       string
		wantErrSub   string
		expectCalled bool // false only when server is closed before the call
	}{
		{
			name:         "happy path returns draft id",
			stub:         stubHandler{status: 200, body: `{"id":"draft_abc123"}`},
			wantID:       "draft_abc123",
			expectCalled: true,
		},
		{
			name:         "401 unauthorized surfaces token expired",
			stub:         stubHandler{status: 401, body: `{"error":"unauthorized"}`},
			wantErrSub:   "token expired",
			expectCalled: true,
		},
		{
			name:         "500 server error surfaces gmail api error",
			stub:         stubHandler{status: 500, body: `{"error":"internal"}`},
			wantErrSub:   "Gmail API error (500)",
			expectCalled: true,
		},
		{
			name:         "200 with non-json body surfaces parse error",
			stub:         stubHandler{status: 200, body: `not-json-at-all`},
			wantErrSub:   "failed to parse response",
			expectCalled: true,
		},
		{
			name:         "200 without a draft id is rejected",
			stub:         stubHandler{status: 200, body: `{}`},
			wantErrSub:   "missing draft id",
			expectCalled: true,
		},
		{
			name:         "network failure when server is closed",
			closeServer:  true,
			wantErrSub:   "failed to create draft",
			expectCalled: false,
		},
	}

	for _, tc := range cases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			var (
				gotMethod string
				gotPath   string
				gotAuth   string
				called    bool
			)

			srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				called = true
				gotMethod = r.Method
				gotPath = r.URL.Path
				gotAuth = r.Header.Get("Authorization")
				// Drain the request body so the client sees a clean response cycle.
				_, _ = io.Copy(io.Discard, r.Body)
				w.Header().Set("Content-Type", "application/json")
				w.WriteHeader(tc.stub.status)
				_, _ = io.WriteString(w, tc.stub.body)
			}))

			baseURL := srv.URL
			if tc.closeServer {
				// Close the server first so the client gets a connection error.
				srv.Close()
			} else {
				defer srv.Close()
			}

			client := NewGmailClientWithBase("test-token", baseURL)
			id, err := client.CreateDraft(context.Background(), newTestMail())

			if tc.wantErrSub == "" {
				if err != nil {
					t.Fatalf("CreateDraft unexpected error: %v", err)
				}
				if id != tc.wantID {
					t.Fatalf("CreateDraft id = %q, want %q", id, tc.wantID)
				}
			} else {
				if err == nil {
					t.Fatalf("CreateDraft expected error containing %q, got nil (id=%q)", tc.wantErrSub, id)
				}
				if !strings.Contains(err.Error(), tc.wantErrSub) {
					t.Fatalf("CreateDraft error = %q, want substring %q", err.Error(), tc.wantErrSub)
				}
				if tc.stub.status == http.StatusUnauthorized && err.Error() != "token expired" {
					t.Fatalf("CreateDraft 401 error = %q, want exact token expired", err.Error())
				}
			}

			if tc.expectCalled {
				if !called {
					t.Fatalf("expected server to be called, wasn't")
				}
				if gotMethod != http.MethodPost {
					t.Errorf("request method = %q, want POST", gotMethod)
				}
				if gotPath != "/drafts" {
					t.Errorf("request path = %q, want /drafts", gotPath)
				}
				if gotAuth != "Bearer test-token" {
					t.Errorf("Authorization header = %q, want %q", gotAuth, "Bearer test-token")
				}
			}
		})
	}
}

func TestGmailClient_CreateDraft_RequestBodyShape(t *testing.T) {
	// Cross-check that the JSON body wraps the base64url-encoded MIME under
	// message.raw per the Gmail drafts API shape. Keeps us honest against
	// accidental refactors of the request envelope.
	var gotRaw string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			Message struct {
				Raw string `json:"raw"`
			} `json:"message"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			t.Errorf("failed to decode request body: %v", err)
		}
		gotRaw = body.Message.Raw
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(200)
		_, _ = io.WriteString(w, `{"id":"abc"}`)
	}))
	defer srv.Close()

	client := NewGmailClientWithBase("t", srv.URL)
	if _, err := client.CreateDraft(context.Background(), newTestMail()); err != nil {
		t.Fatalf("CreateDraft error: %v", err)
	}
	if gotRaw == "" {
		t.Fatal("expected non-empty message.raw in request body")
	}
	// base64url should not contain padding, plus or slash characters.
	if strings.ContainsAny(gotRaw, "+/=") {
		t.Errorf("message.raw contains non-base64url characters: %q", gotRaw)
	}
}

func TestGmailClient_CreateDraft_ContextCanceled(t *testing.T) {
	var called bool
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		called = true
		w.WriteHeader(http.StatusOK)
		_, _ = io.WriteString(w, `{"id":"draft-test-123"}`)
	}))
	defer srv.Close()

	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	client := NewGmailClientWithBase("draft-token", srv.URL)
	_, err := client.CreateDraft(ctx, newTestMail())
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("CreateDraft error = %v, want context.Canceled", err)
	}
	if called {
		t.Fatal("canceled draft request unexpectedly reached the server")
	}
}

func TestGmailClient_SendMessage(t *testing.T) {
	tests := []struct {
		name       string
		status     int
		response   string
		wantID     string
		wantErr    string
		closeFirst bool
	}{
		{
			name:     "success returns message id",
			status:   http.StatusOK,
			response: `{"id":"message-abc123"}`,
			wantID:   "message-abc123",
		},
		{
			name:     "401 preserves token expired contract",
			status:   http.StatusUnauthorized,
			response: `{"error":"unauthorized"}`,
			wantErr:  "token expired",
		},
		{
			name:     "server error surfaces gmail status",
			status:   http.StatusInternalServerError,
			response: `{"error":"internal"}`,
			wantErr:  "Gmail API error (500)",
		},
		{
			name:     "malformed success response surfaces parse error",
			status:   http.StatusOK,
			response: `not-json`,
			wantErr:  "failed to parse response",
		},
		{
			name:     "success response without message id is rejected",
			status:   http.StatusOK,
			response: `{}`,
			wantErr:  "missing message id",
		},
		{
			name:       "network failure surfaces send error",
			closeFirst: true,
			wantErr:    "failed to send message",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var (
				called       bool
				gotMethod    string
				gotPath      string
				gotAuth      string
				gotMediaType string
				gotRaw       string
			)

			srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				called = true
				gotMethod = r.Method
				gotPath = r.URL.Path
				gotAuth = r.Header.Get("Authorization")
				gotMediaType = r.Header.Get("Content-Type")
				var body map[string]json.RawMessage
				if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
					t.Errorf("decode send request: %v", err)
				}
				if len(body) != 1 {
					t.Errorf("send request keys = %v, want only raw", body)
				}
				rawJSON, ok := body["raw"]
				if !ok {
					t.Error("send request is missing top-level raw")
				} else if err := json.Unmarshal(rawJSON, &gotRaw); err != nil {
					t.Errorf("decode send raw field: %v", err)
				}
				w.Header().Set("Content-Type", "application/json")
				w.WriteHeader(tt.status)
				_, _ = io.WriteString(w, tt.response)
			}))
			baseURL := srv.URL
			if tt.closeFirst {
				srv.Close()
			} else {
				defer srv.Close()
			}

			client := NewGmailClientWithBase("send-token", baseURL)
			id, err := client.SendMessage(context.Background(), newTestMail())
			if tt.wantErr == "" {
				if err != nil {
					t.Fatalf("SendMessage unexpected error: %v", err)
				}
				if id != tt.wantID {
					t.Fatalf("SendMessage id = %q, want %q", id, tt.wantID)
				}
			} else {
				if err == nil {
					t.Fatalf("SendMessage expected error containing %q, got nil (id=%q)", tt.wantErr, id)
				}
				if !strings.Contains(err.Error(), tt.wantErr) {
					t.Fatalf("SendMessage error = %q, want substring %q", err.Error(), tt.wantErr)
				}
				if tt.status == http.StatusUnauthorized && err.Error() != "token expired" {
					t.Fatalf("SendMessage 401 error = %q, want exact token expired", err.Error())
				}
			}

			if tt.closeFirst {
				if called {
					t.Fatal("closed server unexpectedly handled send request")
				}
				return
			}
			if !called {
				t.Fatal("expected send request to reach server")
			}
			if gotMethod != http.MethodPost {
				t.Errorf("request method = %q, want POST", gotMethod)
			}
			if gotPath != "/messages/send" {
				t.Errorf("request path = %q, want /messages/send", gotPath)
			}
			if gotAuth != "Bearer send-token" {
				t.Errorf("Authorization header = %q, want Bearer send-token", gotAuth)
			}
			if gotMediaType != "application/json" {
				t.Errorf("Content-Type header = %q, want application/json", gotMediaType)
			}
			if gotRaw == "" {
				t.Fatal("expected non-empty top-level raw message body")
			}
			if strings.ContainsAny(gotRaw, "+/=") {
				t.Errorf("raw message contains non-base64url characters: %q", gotRaw)
			}
		})
	}
}

func TestGmailClient_SendMessage_ContextCanceled(t *testing.T) {
	var called bool
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		called = true
		w.WriteHeader(http.StatusOK)
		_, _ = io.WriteString(w, `{"id":"message-test-123"}`)
	}))
	defer srv.Close()

	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	client := NewGmailClientWithBase("send-token", srv.URL)
	_, err := client.SendMessage(ctx, newTestMail())
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("SendMessage error = %v, want context.Canceled", err)
	}
	if called {
		t.Fatal("canceled send request unexpectedly reached the server")
	}
}

func TestGmailClient_SendMessage_ContextCanceledAfterDispatch(t *testing.T) {
	received := make(chan struct{})
	releaseResponse := make(chan struct{})
	var calls atomic.Int32
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		close(received)
		<-releaseResponse
		w.Header().Set("Content-Type", "application/json")
		_, _ = io.WriteString(w, `{"id":"too-late"}`)
	}))
	defer srv.Close()
	defer close(releaseResponse)

	ctx, cancel := context.WithCancel(context.Background())
	result := make(chan error, 1)
	go func() {
		_, err := NewGmailClientWithBase("send-token", srv.URL).SendMessage(ctx, newTestMail())
		result <- err
	}()

	<-received
	cancel()
	var err error
	select {
	case err = <-result:
	case <-time.After(2 * time.Second):
		t.Fatal("SendMessage did not return promptly after context cancellation")
	}
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("SendMessage error = %v, want context.Canceled", err)
	}
	if got := calls.Load(); got != 1 {
		t.Fatalf("send request count = %d, want exactly 1", got)
	}
}

func TestGmailClient_ResponseBodiesAreBounded(t *testing.T) {
	tests := []struct {
		name string
		call func(*GmailClient) error
	}{
		{
			name: "create draft error body",
			call: func(client *GmailClient) error {
				_, err := client.CreateDraft(context.Background(), newTestMail())
				return err
			},
		},
		{
			name: "send message error body",
			call: func(client *GmailClient) error {
				_, err := client.SendMessage(context.Background(), newTestMail())
				return err
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			const beyondLimitMarker = "RESPONSE_CONTENT_AFTER_LIMIT"
			srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				w.WriteHeader(http.StatusBadGateway)
				_, _ = io.WriteString(w, strings.Repeat("x", maxAPIResponseSize)+beyondLimitMarker)
			}))
			defer srv.Close()

			err := tt.call(NewGmailClientWithBase("bounded-token", srv.URL))
			if err == nil {
				t.Fatal("expected Gmail API error")
			}
			if strings.Contains(err.Error(), beyondLimitMarker) {
				t.Fatal("error included response content beyond maxAPIResponseSize")
			}
			if len(err.Error()) > maxAPIResponseSize+128 {
				t.Fatalf("error length = %d, response body appears unbounded", len(err.Error()))
			}
		})
	}
}

func TestGmailClient_JSONResponsesAreBounded(t *testing.T) {
	tests := []struct {
		name string
		call func(*GmailClient) error
	}{
		{
			name: "create draft response",
			call: func(client *GmailClient) error {
				_, err := client.CreateDraft(context.Background(), newTestMail())
				return err
			},
		},
		{
			name: "send message response",
			call: func(client *GmailClient) error {
				_, err := client.SendMessage(context.Background(), newTestMail())
				return err
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				w.Header().Set("Content-Type", "application/json")
				// A decoder reading directly from LimitReader can accept this valid
				// JSON prefix without ever observing the oversized trailing bytes.
				_, _ = io.WriteString(w, `{"id":"accepted-prefix"}`+strings.Repeat(" ", maxAPIResponseSize))
			}))
			defer srv.Close()

			err := tt.call(NewGmailClientWithBase("bounded-token", srv.URL))
			if err == nil || !strings.Contains(err.Error(), "failed to parse response") {
				t.Fatalf("oversized JSON response error = %v, want bounded parse error", err)
			}
		})
	}
}

func TestGmailClient_ListSendAs(t *testing.T) {
	var called bool
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		called = true
		if r.Method != http.MethodGet {
			t.Errorf("request method = %q, want GET", r.Method)
		}
		if r.URL.Path != "/settings/sendAs" {
			t.Errorf("request path = %q, want /settings/sendAs", r.URL.Path)
		}
		if got := r.Header.Get("Authorization"); got != "Bearer sender-token" {
			t.Errorf("Authorization header = %q, want Bearer sender-token", got)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = io.WriteString(w, `{"sendAs":[{"sendAsEmail":"user@floridaseating.com","displayName":"User Name","replyToAddress":"replies@floridaseating.com","signature":"<p>Signature</p>","isPrimary":true,"isDefault":true,"verificationStatus":"accepted"},{"sendAsEmail":"returns@floridaseating.com","displayName":"Returns","isPrimary":false,"isDefault":false,"verificationStatus":"pending"}]}`)
	}))
	defer srv.Close()

	client := NewGmailClientWithBase("sender-token", srv.URL)
	aliases, err := client.ListSendAs(context.Background())
	if err != nil {
		t.Fatalf("ListSendAs unexpected error: %v", err)
	}
	if !called {
		t.Fatal("expected server to be called")
	}
	if len(aliases) != 2 {
		t.Fatalf("ListSendAs returned %d aliases, want 2", len(aliases))
	}
	if got := aliases[0]; got.SendAsEmail != "user@floridaseating.com" || got.DisplayName != "User Name" || got.ReplyToAddress != "replies@floridaseating.com" || got.Signature != "<p>Signature</p>" || !got.IsPrimary || !got.IsDefault || got.VerificationStatus != "accepted" {
		t.Fatalf("primary alias decoded incorrectly: %+v", got)
	}
	if got := aliases[1]; got.SendAsEmail != "returns@floridaseating.com" || got.VerificationStatus != "pending" {
		t.Fatalf("custom alias decoded incorrectly: %+v", got)
	}
}

func TestGmailClient_ListSendAs_ContextCanceled(t *testing.T) {
	var called bool
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		called = true
		w.WriteHeader(http.StatusOK)
		_, _ = io.WriteString(w, `{"sendAs":[]}`)
	}))
	defer srv.Close()

	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	client := NewGmailClientWithBase("sender-token", srv.URL)
	_, err := client.ListSendAs(ctx)
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("ListSendAs error = %v, want context.Canceled", err)
	}
	if called {
		t.Fatal("canceled request unexpectedly reached the server")
	}
}

func TestGmailClient_ListSendAs_APIError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusForbidden)
		_, _ = io.WriteString(w, `{"error":{"message":"insufficient authentication scopes"}}`)
	}))
	defer srv.Close()

	client := NewGmailClientWithBase("sender-token", srv.URL)
	_, err := client.ListSendAs(context.Background())
	if err == nil || !strings.Contains(err.Error(), "Gmail API error (403)") {
		t.Fatalf("ListSendAs error = %v, want Gmail API error (403)", err)
	}
}

func TestNewGmailClientWithBase_HasBoundedTimeout(t *testing.T) {
	client := NewGmailClientWithBase("sender-token", "https://example.invalid")
	if client.httpClient.Timeout != GmailHTTPTimeout {
		t.Fatalf("http client timeout = %s, want %s", client.httpClient.Timeout, GmailHTTPTimeout)
	}
}
