-- PostgreSQL 初始化脚本
-- 为 Keycloak 和 Teleop Backend 创建数据库和用户

-- 创建 Keycloak 数据库和用户
CREATE DATABASE keycloak_db;
CREATE USER keycloak_user WITH PASSWORD 'keycloak_password_change_in_prod';
GRANT ALL PRIVILEGES ON DATABASE keycloak_db TO keycloak_user;
ALTER DATABASE keycloak_db OWNER TO keycloak_user;

-- Teleop Backend 数据库已在 docker-compose.yml 中通过环境变量创建
-- 这里可以添加额外的初始化脚本，如扩展、函数等

-- 创建必要的扩展（如果需要）
-- CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
-- CREATE EXTENSION IF NOT EXISTS "pg_trgm";

-- 注意：实际表结构将在 M0 阶段的数据库迁移脚本中创建
-- 参考 project_spec.md §4.3 数据模型
