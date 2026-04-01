-- 自动化测试用种子数据：与 Keycloak realm 中 e2e-test 用户（id 即 JWT sub）对应
-- keycloak_sub 必须与 realm-export.json 中 users[0].id 一致
INSERT INTO accounts (id, name) VALUES
  ('b0000000-0000-0000-0000-000000000001'::uuid, 'E2E Test Account')
ON CONFLICT (name) DO NOTHING;

INSERT INTO users (id, keycloak_sub, account_id, username, email) VALUES
  ('c0000000-0000-0000-0000-000000000001'::uuid,
   'a1b2c3d4-e5f6-7890-abcd-ef1234567890',
   'b0000000-0000-0000-0000-000000000001'::uuid,
   'e2e-test',
   'e2e-test@teleop.local')
ON CONFLICT (keycloak_sub) DO NOTHING;

-- 车辆：E2ETESTVIN0000001=车端推流；carla-sim-001/carla-sim-002=CARLA 仿真（Bridge 需配置对应 VIN）
INSERT INTO vehicles (vin, model) VALUES
  ('E2ETESTVIN0000001', 'e2e-test-vehicle'),
  ('carla-sim-001', 'carla-sim'),
  ('carla-sim-002', 'carla-sim')
ON CONFLICT (vin) DO NOTHING;

INSERT INTO account_vehicles (account_id, vin, status) VALUES
  ('b0000000-0000-0000-0000-000000000001'::uuid, 'E2ETESTVIN0000001', 'active'),
  ('b0000000-0000-0000-0000-000000000001'::uuid, 'carla-sim-001', 'active'),
  ('b0000000-0000-0000-0000-000000000001'::uuid, 'carla-sim-002', 'active')
ON CONFLICT (account_id, vin) DO NOTHING;
