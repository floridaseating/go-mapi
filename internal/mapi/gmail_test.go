package mapi

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
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
			id, err := client.CreateDraft(newTestMail())

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
	if _, err := client.CreateDraft(newTestMail()); err != nil {
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
