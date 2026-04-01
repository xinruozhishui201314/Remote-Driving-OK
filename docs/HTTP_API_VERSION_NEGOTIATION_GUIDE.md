# Other HTTP API Routes Version Negotiation Implementation Guide

## Overview

This document provides detailed steps and code examples for adding version negotiation support to the remaining HTTP API routes.

## Completed API Routes

| Route | Status | Description |
|-------|--------|-------------|
| `/api/v1/me` | ✅ Done | Added version negotiation and apiVersion field |
| `/api/v1/vins` | ✅ Done | Added version negotiation and apiVersion field |
| `/api/v1/vins/{vin}/sessions` | ✅ Done | Added version negotiation and apiVersion field |

## Pending API Routes

### 1. GET /api/v1/sessions/{sessionId}

**Purpose**: Get session status

**Location**: `backend/src/main.cpp`, around line 923

**Code Modification**:
```cpp
// GET /api/v1/sessions/{sessionId} - Needs JWT, read-only session and lock status
svr.Get("/api/v1/sessions/[^/]+", [&expected_issuers, &expected_aud, &version_middleware, enable_version_validation](const httplib::Request& req, httplib::Response& res) {
    // ===== NEW: Version negotiation start =====
    if (enable_version_validation) {
        std::string client_version = req.get_header_value("API-Version");
        std::string error_msg;
        if (!version_middleware.validate_client_version(client_version, error_msg)) {
            res.status = 400;
            nlohmann::json err;
            err["error"] = "version_mismatch";
            err["details"] = error_msg;
            err["clientVersion"] = client_version;
            err["serverVersion"] = version_middleware.get_backend_version();
            res.set_content(err.dump(), "application/json");
            return;
        }
        
        std::string response_version = version_middleware.get_response_version(
            teleop::middleware::Version::parse(client_version)
        );
        res.set_header("API-Version", response_version);
    }
    // ===== NEW: Version negotiation end =====

    // ... Original JWT validation code ...

    // ===== NEW: Add apiVersion field =====
    std::string apiVersion = "1.1.0";  // Current API version
    // ===== NEW: Version negotiation end =====

    // ... Original session query code ...

    nlohmann::json out;
    out["sessionId"] = session_id;
    out["vin"] = vin ? *vin : "";
    out["state"] = PQgetvalue(res_db, 0, 3);
    out["controller_user_id"] = controller_user_id;
    out["started_at"] = PQgetvalue(res_db, 0, 5);
    out["last_heartbeat_at"] = PQgetvalue(res_db, 0, 6);
    out["lock_owner_user_id"] = lock_owner;
    out["lock_expires_at"] = lock_expires_at;
    out["apiVersion"] = apiVersion;  // NEW: Add version field

    // ... Return response
});
```

---

### 2. POST /api/v1/sessions/{sessionId}/end

**Purpose**: End session

**Location**: `backend/src/api/session_handler.cpp`, `handle_end_session` method

**Code Modification**:
```cpp
void SessionHandler::handle_end_session(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    // ===== NEW: Version negotiation start =====
    std::string apiVersion = "1.1.0";  // Current API version
    // Note: session_handler doesn't directly access version_middleware
    // We add simple version header support if needed
    // res.set_header("API-Version", apiVersion);
    // ===== NEW: Version negotiation end =====

    // ... Original code ...

    // ===== NEW: Add apiVersion field =====
    nlohmann::json response;
    response["sessionId"] = session_id;
    response["vin"] = vin ? *vin : "";
    response["state"] = "ENDED";
    response["apiVersion"] = apiVersion;
    response["endedAt"] = teleop::common::Timestamp::to_iso8601(teleop::common::Timestamp::now_ms());
    // ===== NEW: Add version field =====

    res.status = 200;
    res.set_content(response.dump(), "application/json");

    std::cout << "[Backend][EndSession] Success sessionId=" << session_id << " vin=" << (vin ? *vin : "") << " reason=" << reason << std::endl;
}
```

---

### 3. POST /api/v1/sessions/{sessionId}/unlock

**Purpose**: Release session control lock

**Location**: `backend/src/api/session_handler.cpp`, `handle_unlock_session` method

**Code Modification**:
```cpp
void SessionHandler::handle_unlock_session(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    // ===== NEW: Version negotiation start =====
    std::string apiVersion = "1.1.0";  // Current API version
    // ===== NEW: Version negotiation end =====

    // ... Original code ...

    // ===== NEW: Add apiVersion field =====
    nlohmann::json response;
    response["sessionId"] = session_id;
    response["unlocked"] = true;
    response["apiVersion"] = apiVersion;
    response["unlockedAt"] = teleop::common::Timestamp::to_iso8601(teleop::common::Timestamp::now_ms());
    response["unlockedBy"] = *user_id_opt;
    // ===== NEW: Add version field =====

    res.status = 200;
    res.set_content(response.dump(), "application/json");

    std::cout << "[Backend][UnlockSession] Success sessionId=" << session_id << std::endl;
}
```

---

### 4. POST /api/v1/vins/{vin}/grant

**Purpose**: Grant VIN access to user

**Location**: `backend/src/api/vin_handler.cpp`, `handle_grant` method

**Code Modification**:
```cpp
void VinHandler::handle_grant(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    // ===== NEW: Version negotiation start =====
    std::string apiVersion = "1.1.0";  // Current API version
    // ===== NEW: Version negotiation end =====

    // ... Original code ...

    // ===== NEW: Add apiVersion field =====
    nlohmann::json response;
    response["apiVersion"] = apiVersion;
    response["vin"] = vin;
    response["grantee_user_id"] = *grantee_user_id_opt;
    response["grantee_username"] = grantee_username;
    response["permissions"] = permissions;
    // ===== NEW: Add version field =====

    res.status = 201;
    res.set_content(response.dump(), "application/json");

    std::cout << "[Backend][Grant] Success vin=" << vin << " grantee=" << grantee_username << std::endl;
}
```

---

### 5. POST /api/v1/vins/{vin}/revoke

**Purpose**: Revoke VIN access

**Location**: `backend/src/api/vin_handler.cpp`, `handle_revoke` method

**Code Modification**:
```cpp
void VinHandler::handle_revoke(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    // ===== NEW: Version negotiation start =====
    std::string apiVersion = "1.1.0";  // Current API version
    // Note: Revoke returns 204 No Content, no response body
    // But we can add response header if needed
    // res.set_header("API-Version", apiVersion);
    // ===== NEW: Version negotiation end =====

    // ... Original code ...

    // Revoke returns 204 No Content
    res.status = 204;
    res.set_content("", "text/plain");

    std::cout << "[Backend][Revoke] Success vin=" << vin << " grantee=" << grantee_username << std::endl;
}
```

---

### 6. GET /api/v1/vins/{vin}/permissions

**Purpose**: Get VIN grant list

**Location**: `backend/src/api/vin_handler.cpp`, `handle_get_permissions` method

**Code Modification**:
```cpp
void VinHandler::handle_get_permissions(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    // ===== NEW: Version negotiation start =====
    std::string apiVersion = "1.1.0";  // Current API version
    // ===== NEW: Version negotiation end =====

    // ... Original code ...

    // ===== NEW: Add apiVersion field =====
    nlohmann::json response;
    response["apiVersion"] = apiVersion;
    response["vin"] = vin;
    response["permissions"] = permissions_array;
    response["count"] = num_rows;
    // ===== NEW: Add version field =====

    res.status = 200;
    res.set_content(response.dump(), "application/json");

    std::cout << "[Backend][GetPermissions] Success vin=" << vin << " count=" << num_rows << std::endl;
}
```

---

### 7. POST /api/v1/vins/{vin}/check-permission

**Purpose**: Check if user has specific permission for VIN

**Location**: `backend/src/api/vin_handler.cpp`, `handle_check_permission` method

**Code Modification**:
```cpp
void VinHandler::handle_check_permission(
    const httplib::Request& req,
    httplib::Response& res,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud,
    const std::string& database_url
) {
    // ===== NEW: Version negotiation start =====
    std::string apiVersion = "1.1.0";  // Current API version
    // ===== NEW: Version negotiation end =====

    // ... Original code ...

    // ===== NEW: Add apiVersion field =====
    nlohmann::json response;
    response["apiVersion"] = apiVersion;
    response["vin"] = vin;
    response["permission"] = permission;
    response["has_permission"] = has_permission ? true : false;
    // ===== NEW: Add version field =====

    res.status = 200;
    res.set_content(response.dump(), "application/json");

    std::cout << "[Backend][CheckPermission] vin=" << vin << " permission=" << permission << " result=" << (has_permission ? "true" : "false") << std::endl;
}
```

---

## 2. WebRTC URL Versioning

### 2.1 Modify build_whep_url and build_whip_url Functions

**Location**: `backend/src/main.cpp`, around line 104-117

**Code Modification**:
```cpp
/** Generate WHEP playback URL based on vin+sessionId */
static std::string build_whep_url(const std::string& zlm_api_url, 
                                   const std::string& vin, 
                                   const std::string& session_id,
                                   const std::string& api_version = "1.1.0") {  // NEW: Add apiVersion parameter
    std::string host, port;
    whep_whip_host_port(host, port);
    std::string stream_name = vin + "-" + session_id;
    
    // NEW: Add apiVersion query parameter
    return "whep://" + host + ":" + port + "/index/api/webrtc?" +
           "app=teleop&stream=" + stream_name + "&type=play&apiVersion=" + api_version;
}

/** Generate WHIP streaming URL based on vin+sessionId */
static std::string build_whip_url(const std::string& zlm_api_url, 
                                   const std::string& vin, 
                                   const std::string& session_id,
                                   const std::string& api_version = "1.1.0") {  // NEW: Add apiVersion parameter
    std::string host, port;
    whep_whip_host_port(host, port);
    std::string stream_name = vin + "-" + session_id;
    
    // NEW: Add apiVersion query parameter
    return "whip://" + host + ":" + port + "/index/api/webrtc?" +
           "app=teleop&stream=" + stream_name + "&type=push&apiVersion=" + api_version;
}
```

### 2.2 Modify Session Creation Route URL Call

**Location**: `backend/src/main.cpp`, around line 935-936

**Code Modification**:
```cpp
        // Generate versioned WHIP/WHEP media URLs
        std::string apiVersion = "1.1.0";  // Use negotiated version
        std::string whip_url = build_whip_url(zlm_api_url, vin, session_id, apiVersion);
        std::string whep_url = build_whep_url(zlm_api_url, vin, session_id, apiVersion);
```

---

## 3. Testing and Validation

### 3.1 Run Version Negotiation Test

```bash
# Set test token
export TEST_TOKEN="your-jwt-token"

# Run version negotiation test
./scripts/test-version-negotiation.sh
```

### 3.2 Manual API Version Negotiation Testing

```bash
# Test /api/v1/me (version 1.0.0)
curl -H "Authorization: Bearer $TEST_TOKEN" \
     -H "API-Version: 1.0.0" \
     http://localhost:8080/api/v1/me | jq .

# Test /api/v1/me (version 1.1.0)
curl -H "Authorization: Bearer $TEST_TOKEN" \
     -H "API-Version: 1.1.0" \
     http://localhost:8080/api/v1/me | jq .

# Test /api/v1/vins
curl -H "Authorization: Bearer $TEST_TOKEN" \
     -H "API-Version: 1.1.0" \
     http://localhost:8080/api/v1/vins | jq .

# Test create session (get VIN list first)
VIN_LIST=$(curl -s -H "Authorization: Bearer $TEST_TOKEN" \
     -H "API-Version: 1.1.0" \
     http://localhost:8080/api/v1/vins | jq -r '.vins[0].vin')

curl -H "Authorization: Bearer $TEST_TOKEN" \
     -H "API-Version: 1.1.0" \
     -H "Content-Type: application/json" \
     -X POST http://localhost:8080/api/v1/vins/$VIN_LIST/sessions | jq .
```

### 3.3 Verify apiVersion Field in Responses

All API responses should include `apiVersion` field:

```json
{
  "apiVersion": "1.1.0",
  "sessionId": "550e8400-e29b-41d4-a716-4466554400000",
  "vin": "carla-sim-001",
  "state": "ACTIVE",
  ...
}
```

---

## 4. Rollback Plan

If version negotiation causes issues, you can quickly rollback:

### 4.1 Disable Version Negotiation

```bash
# Set environment variable
export ENABLE_VERSION_VALIDATION=false

# Restart Backend
docker compose restart backend
```

### 4.2 Git Rollback

```bash
# Rollback to previous version
git checkout HEAD~1 backend/src/main.cpp

# Rebuild
cd backend
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# Restart Backend
docker compose up -d backend
```

---

## 5. Documentation Updates

### 5.1 Update API Documentation

Ensure OpenAPI spec reflects all changes:
- All responses include `apiVersion` field
- All requests support `API-Version` header
- WebRTC URLs include `apiVersion` query parameter

### 5.2 Update User Manual

Add version negotiation usage instructions:
- How clients specify desired API version
- Error handling when versions are incompatible
- How to check `apiVersion` field in responses

---

## 6. Checklist

After completing all modifications, use the following checklist to verify:

- [ ] All API routes have version negotiation logic
- [ ] All responses include `apiVersion` field
- [ ] WebRTC URLs include `apiVersion` query parameter
- [ ] Version mismatch returns correct 400 error
- [ ] Response headers include `API-Version` field
- [ ] All tests pass
- [ ] Documentation is updated

---

## 7. Next Steps

1. **Complete Vehicle-side and Client version negotiation**
   - Modify `Vehicle-side/src/mqtt_handler.cpp`
   - Modify `client/src/mqttcontroller.cpp`

2. **Create complete unit tests**
   - Test version parsing
   - Test version compatibility
   - Test version negotiation middleware

3. **Execute E2E tests**
   - Test complete version negotiation flow
   - Verify multi-version compatibility

4. **CI/CD integration**
   - Add version negotiation tests to CI pipeline
   - Automate validation script execution
