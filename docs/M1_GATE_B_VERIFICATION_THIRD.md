# M1 GATE B 验证：GET /api/v1/vins 占位（第三批）

**对应提案**: [M1_GATE_A_PROPOSAL_THIRD.md](./M1_GATE_A_PROPOSAL_THIRD.md)  
**实施日期**: 2026-02-06

---

## 1. 变更摘要

| 文件 | 变更 |
|------|------|
| `backend/src/main.cpp` | 新增 GET /api/v1/vins：复用 JWT 校验，返回 200 + `{"vins":[]}` |

---

## 2. 验证

- 无 token → 401
- 有效 Bearer token → 200，body `{"vins":[]}`

---

## 3. 运行命令

```bash
docker compose build backend && docker compose up -d backend
curl -s -w "\n%{http_code}\n" http://localhost:8081/api/v1/vins
# 期望: 401 + {"error":"unauthorized"}
curl -s -H "Authorization: Bearer $TOKEN" http://localhost:8081/api/v1/vins
# 期望: 200 + {"vins":[]}
```

---

## 4. 结论

M1 第三批已实现：GET /api/v1/vins 占位接口就绪，后续可接入 DB 返回真实 VIN 列表。
