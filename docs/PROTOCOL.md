# PROTOCOL — network messages

Protocol version: **1** (bump on any incompatible change).
Header (little endian): `u16 protocol · u16 type · u32 seq · u32 len` then payload.
Channels: 0 = reliable ordered (state, facts, scenes) · 1 = unreliable sequenced (transform).

| Type | Name | Dir | Channel | Payload | Since |
|---|---|---|---|---|---|
| 1 | Hello | G→H | 0 | gameVersion, modVersion, protocol, saveHash, playerName | P2.4 |
| 2 | Welcome | H→G | 0 | accepted, reason, hostName | P2.4 |
| 3 | Ping | ↔ | 0 | timestampMs | P2.3 |
| 4 | Pong | ↔ | 0 | timestampMs | P2.3 |
| 5 | Bye | ↔ | 0 | reason | P2.3 |
