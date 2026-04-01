# M1 GATE A 变更提案（第三批）：GET /api/v1/vins 占位接口

**状态**: ✅ 已实施（GATE B 见 [M1_GATE_B_VERIFICATION_THIRD.md](./M1_GATE_B_VERIFICATION_THIRD.md)）  
**日期**: 2026-02-06

---

## 0. Executive Summary

- **目标**：新增 **GET /api/v1/vins**，需 Bearer JWT，当前仅返回 **空数组 `[]`**，为后续“从 DB 查当前用户可见 VIN 列表”占位。
- **收益**：客户端可先对接该 API（鉴权与路由一致）；后续接入 DB/vin_grants 时仅改实现不改契约。
- **非目标**：本批不查数据库、不实现 VIN 授权逻辑；仅 200 + `[]`。

---

## 1. 目标与非目标

| 目标 | 非目标 |
|------|--------|
| GET /api/v1/vins 需 JWT，返回 200 + `{"vins":[]}` | 查 DB、account_vehicles、vin_grants |
| 与 /api/v1/me 共用同一 JWT 校验逻辑 | 车辆在线状态、控制者、故障摘要 |
| 接口契约与 project_spec §5.2 对齐（先空列表） | 会话、流地址、故障 API |

---

## 2. 需求对照

- **§5.2 车辆列表**：用户登录后仅能看到自己被授权的 VIN 列表 → 本批先返回空列表，占位接口。

---

## 3. 实现要点

- 路由：`GET /api/v1/vins`
- 鉴权：与 /me 相同，从 Authorization 取 Bearer，校验 JWT；无效/缺失 → 401
- 响应：`200`，`Content-Type: application/json`，body：`{"vins":[]}`

---

## 4. 测试

- 无 token → 401
- 有效 token → 200，body 含 `"vins":[]`

---

## 5. 变更清单（预估）

| 路径 | 变更 |
|------|------|
| `backend/src/main.cpp` | 注册 GET /api/v1/vins，复用现有 JWT 校验，返回 `{"vins":[]}` |

---

## 6. 运行命令

```bash
docker compose build backend && docker compose up -d backend
curl -s -H "Authorization: Bearer $TOKEN" http://localhost:8081/api/v1/vins
# 期望: {"vins":[]}
```

---

**请确认**：若同意按本提案实施，请回复 **CONFIRM** / **APPROVE** / **GO AHEAD**。
