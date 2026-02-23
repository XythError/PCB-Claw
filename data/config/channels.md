# PCB-Claw Channel Configuration
# Each channel can be enabled/disabled and configured here.
# Lines starting with '#' are ignored.

# ── Serial / USB-CDC Channel ──────────────────────────────────────
serial_enabled: true
serial_baud: 115200

# ── Web Channel (HTTP REST + WebSocket) ───────────────────────────
web_enabled: true
web_port: 80
# Set to true to require HTTP Basic Auth on all endpoints
web_auth_enabled: false
web_auth_user: admin
web_auth_pass: changeme

# ── Telegram Channel ─────────────────────────────────────────────
telegram_enabled: false
# Token from @BotFather — also settable in agent.md
telegram_token:
# Comma-separated list of allowed Telegram chat IDs
# Leave empty to allow all users (not recommended for public bots)
telegram_admins:
# How often to poll getUpdates (milliseconds)
telegram_poll_ms: 5000

# ── Slack Channel (future) ────────────────────────────────────────
slack_enabled: false
slack_bot_token:
slack_channel: #general

# ── SMTP / Mail Channel (future) ─────────────────────────────────
mail_enabled: false
mail_smtp_host:
mail_smtp_port: 587
mail_user:
mail_pass:
mail_from: pcbclaw@example.com

# ── WhatsApp / Business API (future) ─────────────────────────────
whatsapp_enabled: false
whatsapp_token:
whatsapp_phone_id:
