# 增加车辆：正常流程与操作步骤

## 1. 结论摘要

| 项目 | 说明 |
|------|------|
| **规范流程** | 由 **admin 后台绑定**，或 **owner** 使用「VIN + 车辆绑定码」在系统内完成绑定（见 `project_spec.md` §4.2）。 |
| **当前实现** | 后端提供 **网页管理页** 与 **POST /api/v1/vehicles** 接口；车辆绑定到**当前登录用户所在账号**，并**自动为当前用户写入 vin_grants（vin.view、vin.control、vin.maintain）**，整条远驾链路认可，可正常接管。 |
| **实际操作** | **推荐**：浏览器打开管理页 → 填写 JWT + VIN + 型号 → 提交；**或** 直接写库（见下文）。 |
| **持久化** | 车辆写入 **PostgreSQL**，Compose 使用命名卷 **postgres_data**；**下次启动后仍存在**，除非执行删除或清空数据卷。 |

### 1.1 数据持久化说明

- 添加的车辆保存在数据库表 **vehicles**、**account_vehicles** 以及 **vin_grants**（当前用户拥有 vin.view / vin.control / vin.maintain）中，数据落在 **PostgreSQL**。
- Docker Compose 中 Postgres 使用**命名卷** `postgres_data`（见 `docker-compose.yml`），因此：
  - **重启**（`docker compose restart`）或 **停止后再次启动**（`docker compose up -d`）：数据**保留**，下次启动后车辆列表仍包含已添加的车辆。
  - 只有**明确删除车辆**（见下文「删除车辆」）或**删除数据卷**（如 `docker compose down -v` 会清空卷，导致数据丢失）后，车辆才会从列表中消失。
- 若需长期保留数据，**不要**使用 `docker compose down -v`；仅需停止/启动时使用 `docker compose down` 与 `docker compose up -d` 即可。

---

## 2. 网页方式增加车辆（推荐）

通过浏览器打开「增加车辆」管理页，使用**管理员/测试账号**登录获取 JWT 后提交，即可在远驾客户端刷新看到并选择新车。

### 2.1 管理员/测试账号（直接登录用）

用于「增加车辆」管理页获取 JWT、以及远驾客户端选车为**同一账号**，添加后客户端刷新即可看到新车。

| 用途　　　　　　　　　| 账号　　　　 | 密码　　　　　　　　　| 说明　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　 |
| -----------------------| --------------| -----------------------| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 添加车辆 / 客户端选车 | **e2e-test** | **e2e-test-password** | Keycloak 域 `teleop` 下用户（见 `deploy/keycloak/realm-export.json`），与种子数据 `account_vehicles` 对应；用该账号登录后添加的车辆会出现在该账号的选车列表中。若导入 realm 后修改过密码，请使用实际密码。 |

- **获取 JWT**：用上述账号在 Keycloak 登录（见下）或先登录远驾客户端，再从网络请求中复制 `Authorization: Bearer <token>` 的 `<token>` 到管理页。
- **Keycloak 控制台管理员**（仅管理 Keycloak，不用于添加车辆）：`admin` / `admin`（见 `deploy/.env.example` 中 `KEYCLOAK_ADMIN` / `KEYCLOAK_ADMIN_PASSWORD`）。

**重要说明（Keycloak 登录入口）**

- **http://localhost:8080** 打开的是 Keycloak **master** 域的管理员登录（账号 admin / admin），不是业务用户登录。
- 用户 **e2e-test** 属于 **teleop** 域，要在 **teleop** 的登录页或用「获取 Token」页 / 远驾客户端登录，不要在 8080 首页用 e2e-test 登录。
- 若 realm 导入后 e2e-test 仍登不进，多半是**密码未随导入生效**，可执行：`./scripts/keycloak-set-e2e-password.sh` 为 e2e-test 设置密码后再试。

**Keycloak 用户与 Backend 同步（JIT）**

- 在 **http://localhost:8080/admin**（Keycloak 管理台）里新建的用户，**不会**自动出现在 Backend 的 `users` / `accounts` 表中；Backend 用 JWT 的 `sub` 查本库。
- 自当前版本起，Backend 支持 **Just-In-Time（JIT）同步**：当某 JWT 的 `sub` 在 `users` 表中不存在时，**首次**访问「增加车辆」或「车辆列表」等接口会**自动**在库中创建对应 `account`（账号名 `Auto-{sub}`）和 `user` 记录，之后即可正常添加车辆、选车。
- 因此：**在 8080 添加用户后，无需再手动跑同步脚本**；用该用户获取 JWT 后，在 **http://localhost:8081/admin/add-vehicle** 提交一次即可（首次会先同步再添加）。若仍报「user not found or no account」，请确认 (1) JWT 有效、未过期；(2) Backend 已重新编译并重启（包含 JIT 逻辑）；(3) 数据库可写且已执行过 migrations（存在 `users` / `accounts` 表）。
- **自动同步**：`docker-compose` 中已配置 **db-init** 服务，在 Postgres 就绪后自动执行 migrations（001/002/003）与 seed，**无需手动运行脚本**。Backend 依赖 db-init 完成后再启动，因此 add-vehicle 开箱即用。若在非 Compose 环境下部署，可手动执行：`./scripts/ensure-seed-data.sh`。

**方式 A：网页上打开「获取 Token」页（推荐，无需命令行）**

1. 在浏览器打开：**http://localhost:8081/admin/get-token**
2. 用户名为 **e2e-test**，密码填 **e2e-test-password**。Client Secret：若此前用旧版 realm 导入，Keycloak 中 teleop-backend 的密钥可能为 **-change-me-in-production**（前有连字符），请在页面上填写 **-change-me-in-production**；新部署或重新导入 realm 后为 **change-me-in-production**（见 `deploy/keycloak/realm-export.json`）。
3. 点击「获取 Token」，页面会显示 access_token；复制整段 token。
4. 打开 **http://localhost:8081/admin/add-vehicle**，将 token 粘贴到「JWT Token」框，填写 VIN 和车型后提交。

（该页通过后端代理请求 Keycloak，无跨域问题。）

**若 http://localhost:8081/admin/get-token 打不开（404）**：说明当前运行的 backend 未包含该路由，需**重新编译并重启 backend**（dev 模式下会挂载源码，重启后会自动编译；若仍 404 可执行 `./scripts/parallel-compile-verify.sh` 或 `docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml -f docker-compose.dev.yml up -d --build backend`）。

**方式 A2：从远驾客户端请求头复制 JWT**

1. 打开远驾客户端并登录：用 **e2e-test** / **e2e-test-password**，服务器地址填 `http://localhost:8081`（或容器内用 `http://backend:8080`）。
2. 登录成功后，按 **F12** 打开浏览器开发者工具（若客户端是 Qt 窗口，则在**宿主机浏览器**打开任意页如 `http://localhost:8081/admin/add-vehicle`，再在**另一标签页**用同一 Keycloak 账号做一次 OAuth 登录或直接用下面的方式 B）。
3. **若 JWT 从客户端取**：在 Qt 客户端里点击「选车」或「刷新车辆列表」，同时在宿主机用抓包/代理看到请求时，从请求头 `Authorization: Bearer <token>` 中复制 `<token>`；**或** 用方式 B 在浏览器里拿 token。
4. **在浏览器里拿 token（推荐）**：打开 **http://localhost:8081/admin/add-vehicle**，再开一个新标签页访问下面「方式 B」的测试页，用 e2e-test 登录后页面上会显示 access_token，复制到管理页的「JWT Token」框即可。

**方式 B：浏览器里用 Keycloak 登录并显示 token（纯网页操作）**

1. 在浏览器打开：**http://localhost:8080/realms/teleop/account**（Keycloak 用户账号控制台）。
2. 使用 **e2e-test** / **e2e-test-password** 登录。
3. 登录后按 **F12** → 切到 **Console**，输入下面一行回车，即可在控制台看到当前会话的 access_token（若页面有暴露 token 的接口则可用；若无则用方式 C 或从客户端请求头取）：
   ```javascript
   // 若 Keycloak 账号页未直接暴露 token，可改用方式 C（curl）或从远驾客户端请求头复制
   copy(JSON.parse(localStorage.getItem('keycloak') || '{}').token || '请用方式 C 或从客户端请求头复制');
   ```
4. 若上述不可用，则用 **方式 C** 在终端取 token，或从远驾客户端请求头复制。

**方式 C：命令行 curl 获取 JWT**

Realm 中 `teleop-backend` 的 client secret：**新导入**（或使用当前 `realm-export.json` 字面量）为 **change-me-in-production**；**旧版 realm 导入**可能为 **-change-me-in-production**（前有连字符）。以 Keycloak 控制台 Clients → teleop-backend → Credentials 为准。

```bash
# 先试 change-me-in-production；若返回 Invalid client credentials 再试 -change-me-in-production
curl -s -X POST "http://localhost:8080/realms/teleop/protocol/openid-connect/token" \
  -d "client_id=teleop-backend" \
  -d "client_secret=change-me-in-production" \
  -d "username=e2e-test" \
  -d "password=e2e-test-password" \
  -d "grant_type=password" | jq -r '.access_token'
```

若返回 `null` 或 `Invalid client credentials`：试将 `client_secret` 改为 **-change-me-in-production**。并检查：(1) Keycloak 是否运行且 realm `teleop` 已导入；(2) 用户 e2e-test 是否存在、密码是否已设置（可运行 `./scripts/keycloak-set-e2e-password.sh`）；(3) Clients → teleop-backend → Settings 中 **Direct access grants** 为 On。

**若在 http://localhost:8080 用 e2e-test / e2e-test-password 登不进去**：8080 首页是 **master** 域管理员登录，e2e-test 在 **teleop** 域。请改用：(1) **http://localhost:8081/admin/get-token**（需先重建 backend，见上）；或 (2) 命令行方式 C（curl）；或 (3) 先为 e2e-test 设置密码：`./scripts/keycloak-set-e2e-password.sh`，再在 **http://localhost:8080/realms/teleop/account** 用 e2e-test / e2e-test-password 登录。

将得到的 access_token 粘贴到管理页 **http://localhost:8081/admin/add-vehicle** 的「JWT Token」框即可提交添加车辆。

### 2.2 打开管理页

- 后端端口映射到宿主机时，在浏览器访问：  
  **http://localhost:8081/admin/add-vehicle**  
- 若通过 Compose 部署且后端服务名为 `backend`、端口 8081：  
  **http://localhost:8081/admin/add-vehicle**

### 2.3 获取 JWT Token（若未用 2.1 的 curl）

管理页需要当前用户的 **JWT（access_token）** 以识别账号并绑定车辆。

- **方式一**：使用远驾客户端登录（如 e2e-test / 123）后，若客户端或文档提供“复制 Token”入口，可直接粘贴。  
- **方式二**：通过 Keycloak 获取。例如用 Keycloak 的 Direct Grant（Resource Owner Password）或登录后从浏览器开发者工具 / 网络请求中复制 `access_token`。  
- **方式三**：开发/测试时，可先用** e2e-test / e2e-test-password** 登录远驾客户端，再在请求 `/api/v1/vins` 的请求头里复制 `Authorization: Bearer <token>` 中的 `<token>` 部分。

### 2.4 填写并提交

1. 在管理页 **JWT Token** 框中粘贴 access_token（可带或不带 `Bearer ` 前缀）。  
2. **VIN**：必填，新车辆唯一标识（建议 ≤17 位），如 `carla-sim-003`。  
3. **车型/型号**：选填，如 `carla-sim`。  
4. 点击「提交添加」。

成功后会提示“添加成功”；使用**同一账号（e2e-test）**登录远驾客户端，在选车页**刷新或重新进入**即可看到新车并选择。

### 2.5 本地运行后端时显示管理页

若后端在本地直接运行（非 Docker），需让程序能找到静态页面：设置环境变量 **STATIC_DIR** 为 `add-vehicle.html` 所在目录（例如 `backend/static` 的绝对路径），或将该目录挂载/复制到默认路径（默认 `STATIC_DIR=/app/static`）。Docker 镜像已包含 `/app/static/add-vehicle.html`，无需额外配置。

### 2.6 浏览器显示 “This page isn't working” 或打不开

**现象**：打开 http://localhost:8081/admin/add-vehicle 后浏览器显示 “This page isn't working” 或无法打开。

**原因**：当前运行的 backend 镜像多为**旧镜像**，要么没有注册 `GET /admin/add-vehicle` 路由，要么镜像里没有拷贝 `static/add-vehicle.html`（未执行 `COPY static /app/static`）。

**先诊断**（在宿主机执行）：

```bash
# 看 HTTP 状态码
curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8081/admin/add-vehicle

# 看 backend 启动与请求日志（依据日志判断根因）
docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml logs --tail=50 backend
```

**依据日志判断**：

| 日志内容 | 含义 | 处理 |
|----------|------|------|
| `[Backend][启动] add-vehicle.html 可读=否` | 容器内无该文件（未挂载或镜像无 COPY static） | 确认 compose 中 backend 已挂载 `./backend/static:/app/static:ro`，然后 `docker compose up -d backend` 重启 |
| `[Backend][启动] add-vehicle.html 可读=是` | 静态文件在启动时已可读，管理页应能打开 | 若仍 404，见下一条 |
| `[Backend][未匹配] method=GET path=/admin/add-vehicle` | 当前二进制**未注册**该路由（旧镜像） | **必须重新构建 backend 镜像**并重启 |
| `[Backend][GET /admin/add-vehicle] 404 无法打开文件 ... errno=2` | 路由存在但打开文件失败（errno 2=ENOENT 无此文件） | 挂载 `backend/static` 或重建镜像含 COPY static，再重启 |
| `[Backend][GET /admin/add-vehicle] 200 已返回 HTML` | 请求成功 | 正常 |

**处理**：

1. **先让静态文件生效（无需重建镜像）**：compose 已为 backend 增加挂载 `./backend/static:/app/static:ro`。执行一次重启使挂载生效：
   ```bash
   docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d backend
   ```
   查看日志应出现 `[Backend][启动] add-vehicle.html 可读=是`。若仍为「可读=否」，检查宿主机是否存在 `backend/static/add-vehicle.html`。

2. **若仍 404**：说明当前镜像的**二进制没有** GET /admin/add-vehicle 路由。必须**重新构建** backend（需具备 `backend/deps`）：
   ```bash
   docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml build backend
   docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d backend
   ```
   再访问 http://localhost:8081/admin/add-vehicle 并查看日志是否有 `[Backend][GET /admin/add-vehicle] 200 已返回 HTML`。

3. **验证功能**：`./scripts/verify-add-vehicle-e2e.sh`（需设置 COMPOSE_FILES 与当前启动方式一致）。

### 2.7 删除车辆（解除本账号绑定）

已添加的车辆会**一直存在**（见 §1.1 持久化），直到主动删除。若要从**当前账号**的选车列表中移除某车辆（解除绑定）：

- **接口**：`DELETE /api/v1/vehicles/{vin}`，请求头需带 `Authorization: Bearer <JWT>`（同上，使用 e2e-test 的 token）。
- **效果**：从当前用户所在账号的 `account_vehicles` 中删除该 VIN 绑定；该账号在远驾客户端选车页刷新后**不再看到**该车辆。车辆记录仍在 `vehicles` 表中（其他账号若曾绑定仍可见）；若需从库中彻底删除车辆，可执行 SQL：`DELETE FROM vehicles WHERE vin = 'xxx';`（会级联删除相关 `account_vehicles` 等）。
- **示例**：
  ```bash
  curl -X DELETE "http://localhost:8081/api/v1/vehicles/carla-sim-003" \
    -H "Authorization: Bearer <your_access_token>"
  ```
  返回 204 表示已解除绑定；404 表示本账号未绑定该 VIN。解除绑定时后端会**同步删除**该账号下该 VIN 的 `vin_grants`，保证列表与权限一致。

### 2.8 日志与自动化验证

增加/删除车辆的后端日志使用统一前缀 **`[Backend][AddVehicle]`**，便于 grep 与精准分析。

**增加车辆（POST 成功）时，日志顺序应为：**

| 关键字（grep 用） | 含义 | 若缺失可判断 |
|-------------------|------|--------------|
| `[Backend][AddVehicle] vehicles inserted vin=` | 车辆表写入成功 | 车辆表插入失败或未执行 |
| `[Backend][AddVehicle] account_vehicles inserted vin=` | 账号绑定成功 | 账号绑定未执行或失败 |
| `[Backend][AddVehicle] vin_grants inserted vin=` | 权限写入成功（vin.view, vin.control, vin.maintain） | 权限未写入，远驾链路不完整、可能无法正常接管 |
| `[Backend][POST /api/v1/vehicles] 201` | 接口返回 201 | 整体成功 |

**增加车辆失败时：**

| 现象 | 说明 | 处理 |
|------|------|------|
| **失败：user not found or no account** | JWT 有效但 Backend 的 `users` 表中无该 `sub`（Keycloak 新建用户未同步） | 自当前版本起 Backend 支持 **JIT 同步**：首次用该用户访问时会自动创建 account+user。请**重新编译并重启 backend**（确保含 JIT 逻辑）后再试；若仍报错，检查 DATABASE_URL、migrations 已执行、JWT 未过期。 |

| 关键字 | 含义 |
|--------|------|
| `[Backend][AddVehicle] 503 vehicles insert failed` | 车辆表插入失败，看 `err=` |
| `[Backend][AddVehicle] 503 account_vehicles insert failed` | 账号绑定插入失败 |
| `[Backend][AddVehicle] 503 vin_grants insert failed` | 权限表插入失败（如 vin_grants 表不存在或约束冲突） |

**解除绑定（DELETE 成功）时：**

| 关键字 | 含义 |
|--------|------|
| `[Backend][AddVehicle] vin_grants deleted vin=... rows=` | 该账号下该 VIN 的 vin_grants 已同步删除，`rows` 为删除行数 |
| `[Backend][DELETE /api/v1/vehicles/...] 204` | 解除绑定成功 |

**自动化验证脚本**：`./scripts/verify-add-vehicle-e2e.sh` 会执行 POST 添加 → 检查上述三步日志 → DELETE 解除 → 检查 vin_grants deleted 与 204。根据日志断言可精准判断功能是否正常；若某步断言失败，脚本会提示用 `grep '[Backend][AddVehicle]'` 查看后端日志定位问题。

---

## 3. 规范预期（Spec）

- **绑定方式**：  
  1）**admin** 在后台为账号绑定 VIN；或  
  2）**owner** 输入 VIN + 车辆绑定码（车端/出厂贴纸/后台生成）完成绑定。  
- **授权**：owner 可将某 VIN 授权给其他用户（operator/observer/maintenance），并指定权限与有效期。  
- 所有绑定/授权应写入审计日志（当前若仅写库，需后续补审计与接口）。

---

## 4. 直接写库方式（可选）

无需使用网页或 API 时，可**直接操作数据库**增加车辆（例如批量导入、运维脚本）。

### 4.1 需要准备的信息

- **VIN**：新车辆唯一标识（建议 17 位，符合 `vehicles.vin`）。
- **账号 ID（account_id）**：要把车绑定到哪个账号（通常为当前登录用户所在账号）。  
  - 可从现有数据查，例如 e2e-test 账号：`b0000000-0000-0000-0000-000000000001`（见 `deploy/postgres/03_seed_test_data.sql`）。  
  - 或执行：`SELECT a.id, a.name FROM accounts a JOIN users u ON u.account_id = a.id WHERE u.username = 'e2e-test';`

### 4.2 步骤一：在 `vehicles` 表中新增车辆

```sql
INSERT INTO vehicles (vin, model) VALUES
  ('你的VIN', '车型/型号')
ON CONFLICT (vin) DO NOTHING;
```

- `model` 可为任意标识（如 `carla-sim`、`e2e-test-vehicle`），仅作展示/分类用。  
- 若表有 `capabilities`、`safety_profile` 等字段，可按需填写或留空。

### 4.3 步骤二：在 `account_vehicles` 表中绑定到账号

```sql
INSERT INTO account_vehicles (account_id, vin, status) VALUES
  ('b0000000-0000-0000-0000-000000000001'::uuid, '你的VIN', 'active')
ON CONFLICT (account_id, vin) DO NOTHING;
```

- 将 `account_id` 换成目标账号的 UUID。  
- `status = 'active'` 表示该账号可正常使用该车辆；其他可选值见表结构（如 `inactive`、`suspended`）。

### 4.4 步骤三（可选）：授权给其他用户（vin_grants）

若需让**同一账号下其他用户**或**其他账号用户**也能看到/控制该 VIN，需插入 `vin_grants`：

```sql
-- 先查被授权用户 id：SELECT id FROM users WHERE username = '某用户';
INSERT INTO vin_grants (vin, grantee_user_id, permissions, created_by) VALUES
  ('你的VIN', '被授权用户UUID'::uuid, ARRAY['vin.view','vin.control'], '操作者用户UUID'::uuid)
ON CONFLICT (vin, grantee_user_id) DO UPDATE SET permissions = EXCLUDED.permissions;
```

- `permissions` 可选：`vin.view`、`vin.control`、`vin.maintain`。  
- 仅本账号下车辆列表：只做步骤一、二即可；列表由 `account_vehicles` + `vin_grants` 共同决定（见后端 `get_vins_for_sub`）。

---

## 5. 完整示例（复制即用）

以下示例：新增一辆 VIN 为 `carla-sim-003` 的仿真车，并绑定到 e2e-test 所在账号。

```sql
-- 1) 车辆表
INSERT INTO vehicles (vin, model) VALUES
  ('carla-sim-003', 'carla-sim')
ON CONFLICT (vin) DO NOTHING;

-- 2) 绑定到 e2e-test 账号（account_id 来自 03_seed_test_data.sql）
INSERT INTO account_vehicles (account_id, vin, status) VALUES
  ('b0000000-0000-0000-0000-000000000001'::uuid, 'carla-sim-003', 'active')
ON CONFLICT (account_id, vin) DO NOTHING;
```

执行方式示例：

```bash
# 若使用 Docker Compose 中的 Postgres
docker exec -i teleop-postgres psql -U postgres -d teleop_db < 上面保存的.sql

# 或进入容器交互执行
docker exec -it teleop-postgres psql -U postgres -d teleop_db
# 然后粘贴上述 SQL
```

---

## 6. 仿真车特别说明

- **CARLA Bridge** 当前默认只响应 **VIN = carla-sim-001**（环境变量 `VIN`）。  
- 若新增 `carla-sim-002`、`carla-sim-003` 等并在客户端选择它们：  
  - 仅做上述 DB 插入后，**车辆会出现在列表**，可正常创建会话；  
  - 但**推流/控制**需对应 Bridge 也认该 VIN：要么为每个 VIN 起一个 Bridge 容器并设置不同 `VIN`，要么后续改 Bridge 支持多 VIN。  
- 若只做仿真验证：在客户端选 **carla-sim-001** 即可连接当前 CARLA 仿真。

详见：`docs/CARLA_CLIENT_STREAM_GUIDE.md` 中「如何增加仿真车辆」与「与真实车端切换」。

---

## 7. 验证

1. **后端**：用已登录用户的 JWT 请求 `GET /api/v1/vins`，响应中应包含新 VIN。  
2. **客户端**：重新打开选车页（或重新登录/刷新列表），车辆列表中应出现新车辆；选择该车辆后可正常「确认」进入驾驶界面并「连接车端」。

---

## 8. 自动化验证（API + 日志）

增加/删除车辆功能可通过脚本自动验证，**无需操作浏览器**；脚本通过 API 与后端日志判断功能是否正常。

### 8.1 运行方式

- **前提**：Keycloak、Backend、Postgres 已启动（例如已执行 `./scripts/start-all-nodes-and-verify.sh` 或 `docker compose up -d postgres keycloak backend`）。
- **命令**：
  ```bash
  # 若使用默认 docker-compose.yml 启动：
  ./scripts/verify-add-vehicle-e2e.sh

  # 若使用多文件启动（如 start-all-nodes）：
  export COMPOSE_FILES="-f docker-compose.yml -f docker-compose.vehicle.dev.yml"
  ./scripts/verify-add-vehicle-e2e.sh
  ```

### 8.2 验证内容

| 步骤 | 操作 | 判定 |
|------|------|------|
| 1 | 获取 e2e-test 的 JWT | 成功则继续 |
| 2 | GET /api/v1/vins 基线 | 返回 200 |
| 3 | POST /api/v1/vehicles 添加临时 VIN | 返回 201 |
| 4 | GET /api/v1/vins 再次请求 | 响应包含新 VIN |
| 5 | 抓取 backend 日志 | 含 `[Backend][POST /api/v1/vehicles] ... 201` |
| 6 | GET /admin/add-vehicle | 200 且 body 含「增加车辆」 |
| 7 | DELETE /api/v1/vehicles/{vin} | 返回 204 |
| 8 | GET /api/v1/vins 再次请求 | 响应不再包含该 VIN |
| 9 | 抓取 backend 日志 | 含 `[Backend][DELETE ...] 204` |

全部通过则输出「增加/删除车辆 E2E 验证通过」；任一步失败则脚本退出并打印失败步骤与原因。

### 8.3 常见失败与处理

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| POST /api/v1/vehicles 返回 404 | 当前 backend 镜像未包含新接口 | 重新构建并重启 backend：`docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml build backend && ... up -d backend` |
| 无法获取 JWT / 401 | Keycloak 未就绪或 e2e-test 用户不存在 | 等待 Keycloak 健康；确认 realm 已导入且含 e2e-test / e2e-test-password |
| 403 user not found or no account | 该 JWT 的 sub 在 backend users 表中不存在 | 重新编译并重启 backend（启用 JIT 后首次访问会自动同步）；或对 e2e-test 确保已执行 03_seed_test_data.sql 且 keycloak_sub 与 realm 中一致。 |
| 503 / insert failed | 数据库未就绪或 schema 缺失 | 确认 Postgres 健康且已执行 migrations、03_seed_test_data.sql |

### 8.4 修改代码后的流程与依据日志排查

**流程**：修改 backend 代码后需**重新编译 → 重新启动 → 再跑验证**，否则运行的仍是旧进程，新接口（如 POST /api/v1/vehicles）不会生效。

1. **重新编译并重启**（二选一）  
   - **镜像方式**（需具备 backend/deps）：  
     `docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml build backend && docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d backend`  
   - **开发模式**（挂载源码、容器内编译）：使用 `backend/Dockerfile.dev` 与 `docker-entrypoint-dev.sh`，改代码后容器内重新编译并重启进程即可，无需重建镜像。

2. **依据日志判断是否为新 binary**  
   - 启动时 backend 会打印：`[Backend][启动] 路由已注册: ... POST /api/v1/vehicles, DELETE /api/v1/vehicles/{vin} ...`  
   - 若**看不到**该行或该行中**没有**「POST /api/v1/vehicles」，说明当前进程是旧版本，需按上一步重新构建并重启。

3. **依据日志判断 404 原因**  
   - 当 POST /api/v1/vehicles 返回 404 时，若 backend 收到请求但**没有匹配的路由**，会打印：  
     `[Backend][未匹配] method=POST path=/api/v1/vehicles status=404 (无对应路由；若 path 为 /api/v1/vehicles 请确认已重新构建 backend 并重启)`  
   - **结论**：出现该日志即说明请求已到达 backend，但当前进程未注册该路由，**必须重新构建 backend 并重启**后再验证。

4. **验证脚本的自动诊断**  
   - `./scripts/verify-add-vehicle-e2e.sh` 在 POST 返回 404 时会自动抓取 backend 最近日志，若匹配到 `[Backend][未匹配]` 或启动日志中缺少「POST /api/v1/vehicles」，会打印「依据日志」结论和具体处理建议（重新构建并重启 backend）。

---

## 9. 客户端退出登录与验证

### 9.1 功能说明

登录后可在以下两处**退出登录**：

| 位置 | 说明 |
|------|------|
| **车辆选择弹窗** | 登录后弹出的「选择车辆」对话框中，与「刷新列表」并列有 **「退出登录」** 按钮；点击后清除登录状态并关闭弹窗，返回登录页。 |
| **主界面顶栏（状态栏）** | 选车并进入驾驶界面后，顶部状态栏右侧有 **「退出登录」**；点击后返回登录页。 |

退出后：token/用户名被清除并持久化，再次打开客户端或刷新将显示登录页，需重新输入账号密码。

### 9.2 日志关键字（便于排查）

- **C++（AuthManager）**：`[Client][Auth]`  
  - 发起登录：`[Client][Auth] 发起登录: username=... serverUrl=...`  
  - Keycloak 请求：`[Client][Auth] Keycloak token URL: ...`  
  - 登录响应：`[Client][Auth] onLoginReply: HTTP 200 bodySize=...` 或 `HTTP 错误 statusCode=...`、`响应非 JSON`、`Keycloak error=...`、`响应中无 access_token/token`  
  - 登录成功：`[Client][Auth] onLoginReply: 登录成功 username=... tokenLen=... → emit loginSucceeded`  
  - 登录状态更新：`[Client][Auth] 登录状态更新: isLoggedIn=true username=...`  
  - 退出登录：`[Client][Auth] 退出登录: username=...`、`[Client][Auth] 已清除 token/username 并保存; 当前 isLoggedIn=false`
- **C++（Main / 车辆列表）**：`[Client][Main]`、`[Client][车辆列表]`  
  - 登录成功回调：`[Client][Main] loginSucceeded: isTestToken= false ... 调用 loadVehicleList serverUrl=...`  
  - 车辆列表请求：`[Client][车辆列表] 请求 GET .../api/v1/vins hasToken= true tokenLen=...`  
  - 车辆列表响应：`[Client][车辆列表] onVehicleListReply: HTTP 200 bodySize=...` 或 `HTTP 错误`、`响应非 JSON`、`无效格式`  
  - 车辆列表成功：`[Client][车辆列表] 已加载 count= N vins=...`
- **QML（console.log）**：`[Client][UI]`  
  - 登录成功/失败：`[Client][UI] onLoginSucceeded tokenLen=...`、`[Client][UI] onLoginFailed error=...`  
  - 打开选车弹窗：`[Client][UI] openVehicleSelectionTimer: isLoggedIn=...`、`[Client][UI] 打开车辆选择对话框`  
  - 退出登录：`[Client][UI] 车辆选择页点击「退出登录」`、`[Client][UI] 状态栏点击「退出登录」`、`[Client][UI] 登录状态变为未登录，返回登录页并关闭车辆选择弹窗`

### 9.3 验证步骤

1. **编译并启动客户端**（如容器内：`cd /tmp/client-build && cmake /workspace/client -DCMAKE_PREFIX_PATH=... && make -j4 && ./RemoteDrivingClient`）。  
2. **登录**：输入账号密码（如 e2e-test / e2e-test-password），进入车辆选择弹窗。  
3. **在弹窗中退出**：点击「退出登录」→ 弹窗关闭、回到登录页；控制台应出现 `[Client][UI] 车辆选择页点击「退出登录」` 与 `[Client][Auth] 退出登录`。  
4. **再次登录并选车**：进入主驾驶界面后，点击状态栏右侧「退出登录」→ 回到登录页；控制台应出现 `[Client][UI] 状态栏点击「退出登录」` 与 `[Client][Auth] 退出登录`。  
5. **依据日志分析**：若点击退出后未返回登录页，检查是否有 `[Client][Auth] 已清除 token/username` 与 `onLoginStatusChanged(false)`；若没有，检查 `authManager` 是否正确注入与信号连接。

---

## 10. 客户端 e2e-test 登录验证与日志（精准定位闪退）

### 10.1 验证脚本（推荐先跑）

在修改客户端或 Keycloak/Backend 后，先确认「Keycloak 出 JWT → Backend GET /api/v1/vins」整条链路正常：

```bash
./scripts/verify-client-login.sh
```

脚本会：从 Keycloak 用 e2e-test 取 JWT → 用该 JWT 请求 GET /api/v1/vins；任一失败会打印原因并退出。可选：`DO_BUILD_CLIENT=1 ./scripts/verify-client-login.sh` 会在 client-dev 容器内编译客户端并给出运行与日志检查命令。

### 10.2 e2e-test 登录后预期日志顺序

客户端使用 **Keycloak token 接口**登录（非 `/api/auth/login`）。成功时控制台应依次出现：

1. `[Client][Auth] 发起登录: username= e2e-test serverUrl= ...`
2. `[Client][Auth] Keycloak token URL: .../realms/teleop/protocol/openid-connect/token`
3. `[Client][Auth] POST 已发出 url= ...`（请求已发出）
4. `[Client][Auth] onLoginReply: HTTP 200 bodySize= ...`
5. `[Client][Auth] onLoginReply: 登录成功 username= e2e-test tokenLen= ... → emit loginSucceeded`
6. `[Client][UI] onLoginSucceeded tokenLen= ... username= e2e-test`
7. `[Client][Main] loginSucceeded: isTestToken= false ... 调用 loadVehicleList serverUrl= ...`
8. `[Client][车辆列表] 请求 GET .../api/v1/vins hasToken= true tokenLen= ...`
9. `[Client][车辆列表] onVehicleListReply: HTTP 200 bodySize= ...`
10. `[Client][车辆列表] 已加载 count= N vins= ...`
11. `[Client][UI] 打开车辆选择对话框 vehicleSelectionDialog.open()`

若**闪退**，看**最后一条**出现的上述日志，即可判断断点：

| 最后一条日志 | 断点与处理 |
|--------------|------------|
| 无 / 只有「发起登录」 | 请求未发出或立即崩溃 → 检查 serverUrl、Keycloak URL 推导、网络 |
| Keycloak token URL | 请求发出前崩溃 → 检查 QUrl/formBody 等 |
| **errorOccurred: code= ... errorString= ... url= ...** | **网络层错误（连接被拒、超时、DNS 等）→ UI 应显示「网络错误: …」；检查 Keycloak 是否启动、serverUrl 是否为 http://localhost:8080、防火墙** |
| onLoginReply: reply=null（已由 errorOccurred 处理） | 正常：仅表示 finished 在 errorOccurred 之后收到，不再重复弹「无响应」 |
| onLoginReply: HTTP 错误 | Keycloak 返回 4xx/5xx → 检查 Keycloak 与 realm、用户、client_id |
| onLoginReply: 响应非 JSON | Keycloak 返回了 HTML 或错误页 → 看 snippet 内容，确认 URL 是否为 Keycloak |
| Keycloak error= / 响应中无 access_token | 认证失败或格式不符 → 看 error_description / 响应 keys |
| 登录成功 → emit loginSucceeded | 成功后又崩溃 → 检查 main 中 loadVehicleList 或 QML 打开弹窗逻辑 |
| 请求 GET .../api/v1/vins | 车辆列表请求发出后崩溃 → 检查 reply 回调、m_currentReply 置空 |
| onVehicleListReply: HTTP 错误 | Backend 返回 401/5xx → 检查 JWT、Backend 与 GET /api/v1/vins |
| 已加载 count= N | 列表拉取成功，崩溃在弹窗或后续 → 检查 VehicleSelectionDialog、vehicleList 绑定 |

### 10.3 日志输出到终端与文件（闪退后精准分析）

- 客户端启动时若设置环境变量 **CLIENT_LOG_FILE**（或使用脚本默认），则 **qDebug/qWarning 等会同时写入该文件**（带时间戳），并继续输出到 stderr。  
- **一键启动**（`./scripts/start-all-nodes-and-verify.sh`）时，客户端在容器内将日志写入 **/tmp/remote-driving-client.log**。  
- **查看日志**（容器内）：  
  `docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml exec client-dev tail -f /tmp/remote-driving-client.log`  
- **复制到宿主机**：  
  `docker compose ... exec client-dev cat /tmp/remote-driving-client.log > ./client.log`  
- 闪退后打开 client.log，按 10.2 的「最后一条日志」定位断点。
- **若此前出现「登录失败：无响应」**：多为 Keycloak 不可达（未启动或地址/端口错误）。现已修复为：先由 `errorOccurred` 打出 `[Client][Auth] errorOccurred: code= ... errorString= ... url= ...` 并让 UI 显示真实错误（如「网络错误: Connection refused」），`finished` 收到 null 时不再重复 emit「无响应」。请根据 errorString 和 url 排查 Keycloak 与 serverUrl。

### 10.4 「网络错误: Connection refused」彻底解决

| 运行方式 | 原因 | 处理 |
|----------|------|------|
| **客户端在 client-dev 容器内运行** | 界面默认服务器地址为 `http://localhost:8081`，容器内 localhost 无 Backend/Keycloak → 连接被拒 | 已修复：client-dev 容器内设置 `DEFAULT_SERVER_URL=http://backend:8080`，登录页默认显示该地址，Keycloak 请求会发往 `http://keycloak:8080/...`（同网络可达）。重新构建并启动客户端后直接使用默认地址即可用 e2e-test 登录。 |
| **客户端在宿主机运行** | 默认 `http://localhost:8081`，Keycloak 在 8080；若未启动 Keycloak 或端口未映射则 Connection refused | 先启动栈：`docker compose -f docker-compose.yml -f docker-compose.vehicle.dev.yml up -d keycloak backend`，确认 `curl -s http://localhost:8080/health/ready` 或 `./scripts/verify-client-login.sh` 通过后再登录。 |

- 若需在宿主机运行客户端但 Backend/Keycloak 在远程机器，将登录页「服务器地址」改为该机器地址（如 `http://192.168.1.100:8081`），客户端会自动把 Keycloak 推导为同 host 的 8080 端口。

### 10.5 登录页节点状态检测

客户端启动后，登录界面会显示**节点状态**区域（Backend、Keycloak、ZLM），便于部署到不同服务器时快速确认各节点是否可达：

- **自动检测**：打开登录页后会自动根据当前「服务器地址」发起一次检测。
- **手动刷新**：点击「检测」按钮可重新探测；修改服务器地址后建议再点一次「检测」。
- **状态含义**：正常（绿）、不可达/异常（红）、检测中/—（灰）。日志前缀：`[Client][节点检测]`（refresh、Backend/Keycloak/ZLM 正常|不可达、全部完成）。
- **验证脚本**：与客户端逻辑一致的探测脚本，可在无 UI 环境验证节点可达性：  
  `./scripts/verify-login-node-status.sh`（可选 `SERVER_URL=http://backend:8080` 或 `http://localhost:8081`）。

### 10.6 客户端在容器内启动不了（无界面 / libGL / nvidia-drm）

**运行环境固化**：宿主机首次或客户端无界面时请执行一次 `bash scripts/setup-host-for-client.sh`（执行 `xhost +local:docker` 等）。详见 ** [docs/RUN_ENVIRONMENT.md](RUN_ENVIRONMENT.md)**。

**现象**：一键启动后提示「远驾客户端已在容器内启动」，但窗口未弹出或进程很快退出；容器内 `/tmp/remote-driving-client.log` 不存在或仅有少量输出。

**常见原因**：client-dev 容器内没有宿主机 NVIDIA 驱动，Qt/OpenGL 尝试加载 `nvidia-drm` 失败：

```text
libGL error: MESA-LOADER: failed to open nvidia-drm: ... nvidia-drm_dri.so: No such file or directory
libGL error: failed to load driver: nvidia-drm
```

**处理**：

1. **已纳入一键脚本**：`start-all-nodes-and-verify.sh` 启动客户端时已传入 `LIBGL_ALWAYS_SOFTWARE=1`，强制使用软件渲染，避免加载 nvidia-drm。请拉取最新脚本后重新执行一键启动。
2. **手动启动时**：在 exec 前加上该环境变量，例如：  
   `docker compose ... exec -it -e DISPLAY=:0 -e LIBGL_ALWAYS_SOFTWARE=1 ... client-dev bash -c '... ./RemoteDrivingClient --reset-login'`
3. **需要 GPU 加速时**：宿主机安装 nvidia-container-toolkit，compose 中 client-dev 使用 `deploy: resources: reservations: devices: [driver: nvidia]` 并挂载相应设备，则可不设 `LIBGL_ALWAYS_SOFTWARE=1`。

**查看日志**：若仍启动失败，可在容器内前台运行以查看完整 stderr：  
`docker compose ... exec -it -e DISPLAY=:0 -e LIBGL_ALWAYS_SOFTWARE=1 -e CLIENT_LOG_FILE=/tmp/remote-driving-client.log client-dev bash -c 'cd /tmp/client-build && ./RemoteDrivingClient --reset-login 2>&1 | tee /tmp/remote-driving-client.log'`  
再在另一终端复制日志：`docker compose ... exec client-dev cat /tmp/remote-driving-client.log > ./client.log`。

### 10.7 修改后必做验证步骤（强制）

**修改代码后一定要编译并运行自动化验证**，执行一条命令即可完成「编译 backend + 编译 client + 登录链路 + 管理页 + 增加车辆 E2E」：

```bash
./scripts/build-and-verify.sh
```

脚本通过即表示：Backend 与 Client 已重新编译，且登录链路、管理页、增加/删除车辆 E2E 均通过。若某项失败，请根据脚本输出修复后重跑。

其余可选步骤：

1. **单独验证登录链路**：`./scripts/verify-client-login.sh`。  
2. **节点状态验证**：`./scripts/verify-login-node-status.sh`（与登录页「检测」逻辑一致）。  
3. **登录页 UI 验证**：`./scripts/verify-login-ui-features.sh`（账户名历史、密码可见切换及 [Client][Auth]/[Client][UI] 日志）。  
4. **运行客户端并复现**：用 e2e-test / e2e-test-password 登录，若仍闪退则查看容器内 `/tmp/remote-driving-client.log` 或复制到宿主机 `./client.log`。  
5. **按 10.2 表**：用最后一条 `[Client][Auth]` / `[Client][车辆列表]` / `[Client][Main]` / `[Client][UI]` 定位断点并修复。

**登录页行为**：账户名会保存历史（最多 10 条），下次打开可自动填充上次账户名或点击历史按钮选择；密码框右侧提供「显示」/「隐藏」切换明文。相关日志：`[Client][Auth] loadCredentials/保存账户名历史`、`[Client][UI] 已填充上次账户名/选择历史账户名/密码框 可见=`。

---

## 11. 后续演进（可选）

- 实现 **admin**：`POST /api/v1/admin/vehicles` 或 `POST /api/v1/vins/bind`（需 admin 角色），写入 `vehicles` + `account_vehicles` 并写审计日志。
- 实现 **owner 绑定**：`POST /api/v1/vins/bind`（owner 带 VIN + 绑定码），校验绑定码后写入 `account_vehicles`。
- 上述接口实现后，「增加车辆」的正常流程将改为在管理后台或客户端「绑定车辆」入口完成，无需直接写库。
