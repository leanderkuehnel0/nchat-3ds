const express = require('express');
const { Client, LocalAuth } = require('whatsapp-web.js');
const qrcode = require('qrcode-terminal');

const app = express();
app.use(express.json());

const client = new Client({
    authStrategy: new LocalAuth(),
    puppeteer: {
        args: ['--no-sandbox']
    }
});

client.on('qr', (qr) => {
    qrcode.generate(qr, {small: true});
    console.log('Scan the QR code above with your WhatsApp app.');
});

client.on('ready', () => {
    console.log('WhatsApp Client is ready!');
});

// 1. GET /api/chats
app.get('/api/chats', async (req, res) => {
    try {
        const chats = await client.getChats();
        // Return top 20 chats
        const response = chats.slice(0, 20).map(c => ({
            id: c.id._serialized,
            name: c.name || c.id.user,
            unread: c.unreadCount
        }));
        res.json(response);
    } catch (err) {
        res.status(500).json({error: err.toString()});
    }
});

// 2. GET /api/messages?chatId=...
app.get('/api/messages', async (req, res) => {
    try {
        const chatId = req.query.chatId;
        const chat = await client.getChatById(chatId);
        const messages = await chat.fetchMessages({limit: 20});
        
        const response = await Promise.all(messages.map(async m => {
            let sender = "Unknown";
            if (m.fromMe) {
                sender = "You";
            } else if (chat.isGroup) {
                const contact = await m.getContact();
                sender = contact.pushname || contact.name || contact.number;
            } else {
                sender = chat.name;
            }

            return {
                id: m.id._serialized,
                sender: sender,
                text: m.body || (m.hasMedia ? "[Media]" : ""),
                isMe: m.fromMe
            };
        }));
        
        res.json(response);
    } catch (err) {
        res.status(500).json({error: err.toString()});
    }
});

// 3. POST /api/messages
app.post('/api/messages', async (req, res) => {
    try {
        const { chatId, text } = req.body;
        await client.sendMessage(chatId, text);
        res.json({success: true});
    } catch (err) {
        res.status(500).json({error: err.toString()});
    }
});

client.initialize();

const PORT = process.env.PORT || 8080;
app.listen(PORT, () => {
    console.log(`Bridge server listening on port ${PORT}`);
});
