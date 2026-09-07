#!/usr/bin/env python3
"""
Bridge API Verification Script

This script verifies that the WhatsApp bridge API (Node.js/Express server)
is running and responding correctly to all three endpoints.

Usage:
    python3 verify_bridge_api.py [--url BASE_URL] [--chat-id CHAT_ID]

The bridge API endpoints tested:
1. GET /api/chats - List chats
2. GET /api/messages?chatId=... - Get messages for a chat
3. POST /api/messages - Send a message (requires --chat-id)
"""

def test_health(base_url: str) -> bool:
    """Test if the server is reachable at all."""
    try:
        resp = requests.get(base_url, timeout=5)
        print(f"  Server root: HTTP {resp.status_code}")
        return resp.status_code < 500
    except requests.exceptions.ConnectionError:
        print("  ✗ Connection refused - is the bridge server running?")
        return False
    except requests.exceptions.Timeout:
        print("  ✗ Connection timeout")
        return False
    except Exception as e:
        print(f"  ✗ Unexpected error: {e}")
        return False


def test_get_chats(base_url: str) -> Optional[list]:
    """Test GET /api/chats endpoint."""
    url = f"{base_url}/api/chats"
    print(f"\n[GET] {url}")
    
    try:
        resp = requests.get(url, timeout=10)
        print(f"  Status: {resp.status_code}")
        
        if resp.status_code == 200:
            try:
                chats = resp.json()
                print(f"  ✓ Success - received {len(chats)} chats")
                for i, chat in enumerate(chats[:5]):
                    print(f"    {i+1}. {chat.get('name', 'Unknown')} (id: {chat.get('id', 'N/A')[:30]}, unread: {chat.get('unread', 0)})")
                if len(chats) > 5:
                    print(f"    ... and {len(chats) - 5} more")
                return chats
            except json.JSONDecodeError:
                print(f"  ✗ Invalid JSON response: {resp.text[:200]}")
                return None
        else:
            print(f"  ✗ Error: {resp.text[:200]}")
            return None
            
    except requests.exceptions.Timeout:
        print("  ✗ Request timeout")
        return None
    except Exception as e:
        print(f"  ✗ Error: {e}")
        return None


def test_get_messages(base_url: str, chat_id: str) -> Optional[list]:
    """Test GET /api/messages?chatId=... endpoint."""
    url = f"{base_url}/api/messages"
    params = {"chatId": chat_id}
    print(f"\n[GET] {url}?chatId={chat_id[:50]}...")
    
    try:
        resp = requests.get(url, params=params, timeout=10)
        print(f"  Status: {resp.status_code}")
        
        if resp.status_code == 200:
            try:
                messages = resp.json()
                print(f"  ✓ Success - received {len(messages)} messages")
                for i, msg in enumerate(messages[:5]):
                    sender = msg.get('sender', 'Unknown')
                    text = msg.get('text', '')[:60]
                    is_me = " (you)" if msg.get('isMe') else ""
                    print(f"    {i+1}. {sender}{is_me}: {text}")
                if len(messages) > 5:
                    print(f"    ... and {len(messages) - 5} more")
                return messages
            except json.JSONDecodeError:
                print(f"  ✗ Invalid JSON response: {resp.text[:200]}")
                return None
        else:
            print(f"  ✗ Error: {resp.text[:200]}")
            return None
            
    except requests.exceptions.Timeout:
        print("  ✗ Request timeout")
        return None
    except Exception as e:
        print(f"  ✗ Error: {e}")
        return None


def test_post_message(base_url: str, chat_id: str, text: str = "Test from verify script") -> bool:
    """Test POST /api/messages endpoint."""
    url = f"{base_url}/api/messages"
    payload = {"chatId": chat_id, "text": text}
    print(f"\n[POST] {url}")
    print(f"  Payload: {json.dumps(payload)}")
    
    try:
        resp = requests.post(url, json=payload, timeout=10)
        print(f"  Status: {resp.status_code}")
        
        if resp.status_code == 200:
            try:
                result = resp.json()
                if result.get("success"):
                    print(f"  ✓ Message sent successfully")
                    return True
                else:
                    print(f"  ✗ API returned success=false: {result}")
                    return False
            except json.JSONDecodeError:
                print(f"  ✗ Invalid JSON response: {resp.text[:200]}")
                return False
        else:
            print(f"  ✗ Error: {resp.text[:200]}")
            return False
def main():
    parser = argparse.ArgumentParser(
        description="Verify the WhatsApp bridge API is working",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        "--url", "-u",
        default="http://192.168.178.145:8080",
        help="Base URL of the bridge API (default: http://192.168.178.145:8080)"
    )
    parser.add_argument(
        "--chat-id", "-c",
        help="Chat ID to test messages endpoint (required for POST test)"
    )
    parser.add_argument(
        "--test-message",
        default="Test message from verify_bridge_api.py",
        help="Text to send in POST test (default: 'Test message from verify_bridge_api.py')"
    )
    parser.add_argument(
        "--skip-post",
        action="store_true",
        help="Skip the POST /api/messages test"
    )
    args = parser.parse_args()
    main
    base_url = args.url.rstrip("/")
    
    print("=" * 60)
    print("WhatsApp Bridge API Verification")
    print("=" * 60)
    print(f"Base URL: {base_url}")
    print()
    
    # Test 1: Server health
    print("[1/4] Testing server connectivity...")
    if not test_health(base_url):
        print("\n✗ Server is not reachable. Exiting.")
        sys.exit(1)
    
    # Test 2: GET /api/chats
    print("\n[2/4] Testing GET /api/chats...")
    chats = test_get_chats(base_url)
    
    if not chats:
        print("\n✗ Failed to fetch chats. Exiting.")
        sys.exit(1)
    
    # Test 3: GET /api/messages (if chat_id provided or pick first chat)
    test_chat_id = args.chat_id
    if not test_chat_id and chats:
        test_chat_id = chats[0]["id"]
        print(f"\n[3/4] Testing GET /api/messages (using first chat: {test_chat_id[:30]}...)...")
    elif test_chat_id:
        print(f"\n[3/4] Testing GET /api/messages (using provided chat_id)...")
    else:
        print("\n[3/4] Skipping GET /api/messages - no chats available and no --chat-id provided")
        test_chat_id = None
    
    messages = None
    if test_chat_id:
        messages = test_get_messages(base_url, test_chat_id)
    
    # Test 4: POST /api/messages
    if not args.skip_post and test_chat_id:
        print(f"\n[4/4] Testing POST /api/messages...")
        test_post_message(base_url, test_chat_id, args.test_message)
    elif args.skip_post:
        print("\n[4/4] Skipping POST test (--skip-post specified)")
    else:
        print("\n[4/4] Skipping POST test - no chat ID available")
    
    print("\n" + "=" * 60)
    print("Verification complete!")
    print("=" * 60)


if __name__ == "__main__":
    main()
            
    except requests.exceptions.Timeout:
        print("  ✗ Request timeout")
        return False
    except Exception as e:
        print(f"  ✗ Error: {e}")
        return False
import argparse
import json
import sys
import time
from typing import Optional

import requests