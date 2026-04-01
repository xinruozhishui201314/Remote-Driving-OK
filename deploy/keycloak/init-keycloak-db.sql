-- 为 Keycloak 创建独立数据库（与 teleop 业务库分离）
-- 仅在数据库不存在时创建，支持重复执行
DO
$$
BEGIN
    IF NOT EXISTS (SELECT FROM pg_database WHERE datname = 'keycloak') THEN
        CREATE DATABASE keycloak;
    END IF;
END
$$;
