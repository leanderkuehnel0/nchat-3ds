const express = require('express');
const { Client, LocalAuth } = require('whatsapp-web.js');
const qrcode = require('qrcode-terminal');

const app = express();
app.use(express.json());

// -------------------------
// WhatsApp client
// -------------------------

const client = new Client({
    authStrategy: new LocalAuth({
        clientId: 'bridge'
    }),
    puppeteer: {
        headless: true,
        args: [
            '--no-sandbox',
            '--disable-setuid-sandbox'
        ]
    }
});

let isReady = false;

client.on('qr', (qr) => {
    console.log('\nScan this QR code with WhatsApp:\n');
    qrcode.generate(qr, { small: true });
});

client.on('authenticated', () => {
    console.log('WhatsApp authenticated.');
});

client.on('auth_failure', (msg) => {
    console.error('WhatsApp authentication failed:', msg);
    isReady = false;
});

client.on('ready', () => {
    isReady = true;
    console.log('WhatsApp Client is ready!');
});

client.on('disconnected', (reason) => {
    isReady = false;
    console.log('WhatsApp disconnected:', reason);
});

// -------------------------
// Middleware
// -------------------------

function requireWhatsApp(req, res, next) {
    if (!isReady) {
        return res.status(503).json({
            error: 'WhatsApp client is not ready yet.'
        });
    }

    next();
}

// -------------------------
// GET /api/chats
// -------------------------

app.get('/api/chats', requireWhatsApp, async (req, res) => {
    try {
        const chats = await client.getChats();

        const response = chats
            .slice(0, 20)
            .map(chat => ({
                id: chat.id._serialized,
                name: chat.name || chat.id.user || 'Unknown',
                unread: chat.unreadCount || 0,
                isGroup: chat.isGroup
            }));

        res.json(response);
    } catch (err) {
        console.error('Error getting chats:', err);

        res.status(500).json({
            error: err.message || String(err)
        });
    }
});

// -------------------------
// GET /api/messages?chatId=...
// -------------------------

app.get('/api/messages', requireWhatsApp, async (req, res) => {
    try {
        const { chatId } = req.query;

        if (!chatId) {
            return res.status(400).json({
                error: 'chatId is required'
            });
        }

        const chat = await client.getChatById(chatId);

        if (!chat) {
            return res.status(404).json({
                error: 'Chat not found'
            });
        }

        const messages = await chat.fetchMessages({
            limit: 20
        });

        const response = await Promise.all(
            messages.map(async (message) => {
                let sender = 'Unknown';

                if (message.fromMe) {
                    sender = 'You';
                } else {
                    try {
                        const contact = await message.getContact();

                        sender =
                            contact.pushname ||
                            contact.name ||
                            contact.shortName ||
                            contact.number ||
                            'Unknown';
                    } catch {
                        sender = 'Unknown';
                    }
                }

                let text = message.body || '';

                if (!text && message.hasMedia) {
                    text = '[Media]';
                }

                return {
                    id: message.id._serialized,
                    sender,
                    text,
                    isMe: message.fromMe,
                    timestamp: message.timestamp,
                    type: message.type
                };
            })
        );

        res.json(response);
    } catch (err) {
        console.error('Error getting messages:', err);

        res.status(500).json({
            error: err.message || String(err)
        });
    }
});

// -------------------------
// POST /api/messages
// -------------------------

app.post('/api/messages', requireWhatsApp, async (req, res) => {
    try {
        const { chatId, text } = req.body;

        if (!chatId) {
            return res.status(400).json({
                error: 'chatId is required'
            });
        }

        if (!text || typeof text !== 'string') {
            return res.status(400).json({
                error: 'text is required'
            });
        }

        const message = await client.sendMessage(chatId, text);

        res.json({
            success: true,
            messageId: message.id._serialized
        });
    } catch (err) {
        console.error('Error sending message:', err);

        res.status(500).json({
            error: err.message || String(err)
        });
    }
});

// -------------------------
// Health check
// -------------------------

app.get('/api/status', (req, res) => {
    res.json({
        whatsapp: isReady ? 'ready' : 'not_ready'
    });
});

// -------------------------
// Start server
// -------------------------

const PORT = process.env.PORT || 8080;

app.listen(PORT, () => {
    console.log(`Bridge server listening on port ${PORT}`);
});

client.initialize();

