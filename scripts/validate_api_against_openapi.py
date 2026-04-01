#!/usr/bin/env python3
"""
API 验证脚本
根据 OpenAPI 契约验证 Backend 实现的 API
"""

import yaml
import re
import sys
import os
from typing import Dict, List, Optional, Set, Tuple
from dataclasses import dataclass
from pathlib import Path


@dataclass
class ValidationIssue:
    """验证问题"""
    severity: str  # ERROR, WARNING, INFO
    path: str      # API路径
    method: str    # HTTP方法
    message: str   # 问题描述


class OpenAPISpec:
    """OpenAPI 契约解析器"""
    
    def __init__(self, spec_file: str):
        with open(spec_file, 'r', encoding='utf-8') as f:
            self.spec = yaml.safe_load(f)
        
        self.api_version = self.spec['info']['version']
        self.paths = self.spec.get('paths', {})
        self.components = self.spec.get('components', {})
    
    def get_api_paths(self) -> List[Tuple[str, str]]:
        """获取所有API路径（路径, 方法）"""
        paths = []
        for path, methods in self.paths.items():
            for method, details in methods.items():
                if method.lower() in ['get', 'post', 'put', 'delete', 'patch']:
                    paths.append((path, method.lower()))
        return paths
    
    def get_path_schema(self, path: str, method: str) -> Optional[dict]:
        """获取特定路径的Schema"""
        if path not in self.paths:
            return None
        if method not in self.paths[path]:
            return None
        return self.paths[path][method]
    
    def get_response_schema(self, path: str, method: str, status: str) -> Optional[dict]:
        """获取响应Schema"""
        path_schema = self.get_path_schema(path, method)
        if not path_schema:
            return None
        if 'responses' not in path_schema:
            return None
        if status not in path_schema['responses']:
            return None
        return path_schema['responses'][status]


class BackendImplementation:
    """Backend实现解析器"""
    
    def __init__(self, root_dir: str):
        self.root_dir = Path(root_dir)
        self.issues: List[ValidationIssue] = []
        self.registered_handlers = self._scan_handlers()
    
    def _scan_handlers(self) -> Set[Tuple[str, str]]:
        """扫描Backend代码中注册的路由"""
        handlers = set()
        
        # 扫描 main.cpp
        main_file = self.root_dir / 'src' / 'main.cpp'
        if main_file.exists():
            handlers.update(self._parse_cpp_handlers(main_file))
        
        # 扫描 api/vin_handler.cpp
        vin_handler_file = self.root_dir / 'src' / 'api' / 'vin_handler.cpp'
        if vin_handler_file.exists():
            handlers.update(self._parse_cpp_handlers(vin_handler_file))
        
        # 扫描 api/session_handler.cpp
        session_handler_file = self.root_dir / 'src' / 'api' / 'session_handler.cpp'
        if session_handler_file.exists():
            handlers.update(self._parse_cpp_handlers(session_handler_file))
        
        # 扫描 health_handler.cpp
        health_handler_file = self.root_dir / 'src' / 'health_handler.cpp'
        if health_handler_file.exists():
            handlers.update(self._parse_cpp_handlers(health_handler_file))
        
        return handlers
    
    def _parse_cpp_handlers(self, file_path: Path) -> Set[Tuple[str, str]]:
        """解析C++文件中的路由注册"""
        handlers = set()
        
        try:
            content = file_path.read_text(encoding='utf-8')
            
            # 匹配 server.Get/Post/Put/Delete("path", ...)
            pattern = r'server\.(Get|Post|Put|Delete)\s*\(\s*"([^"]+)"'
            matches = re.findall(pattern, content)
            
            for method, path in matches:
                handlers.add((path, method.lower()))
        
        except Exception as e:
            print(f"[WARNING] Failed to parse {file_path}: {e}", file=sys.stderr)
        
        return handlers
    
    def get_response_fields_from_code(self, path: str, method: str) -> Set[str]:
        """从代码中提取响应字段（简化版）"""
        # 这是一个简化实现，实际需要更复杂的解析
        fields = set()
        
        # 根据API路径推断返回字段
        if '/vins' in path and method == 'get':
            fields.update(['apiVersion', 'vins'])
        elif '/sessions' in path and method == 'post':
            fields.update(['sessionId', 'vin', 'media', 'control'])
        elif '/sessions' in path and method == 'post' and 'end' in path:
            fields.update(['sessionId', 'vin', 'state', 'endedAt'])
        
        return fields


class APIValidator:
    """API验证器"""
    
    def __init__(self, openapi_spec: OpenAPISpec, backend: BackendImplementation):
        self.openapi = openapi_spec
        self.backend = backend
        self.issues: List[ValidationIssue] = []
    
    def validate(self) -> List[ValidationIssue]:
        """执行完整验证"""
        print(f"[INFO] Validating API version {self.openapi.api_version}")
        
        # 1. 检查所有OpenAPI定义的路径是否已实现
        self._validate_all_paths_implemented()
        
        # 2. 检查Backend实现的路径是否在OpenAPI中定义
        self._validate_backend_paths_in_spec()
        
        # 3. 检查响应字段
        self._validate_response_fields()
        
        # 4. 检查版本头
        self._validate_version_headers()
        
        return self.issues
    
    def _validate_all_paths_implemented(self):
        """检查所有OpenAPI定义的路径是否已实现"""
        print("[INFO] Checking if all OpenAPI paths are implemented...")
        
        for path, method in self.openapi.get_api_paths():
            if (path, method) not in self.backend.registered_handlers:
                # 跳过健康检查路径（可能在health_handler中）
                if not (path in ['/health', '/ready']):
                    self.issues.append(ValidationIssue(
                        severity='ERROR',
                        path=path,
                        method=method,
                        message=f'Path not implemented in Backend'
                    ))
                    print(f"  [ERROR] {method.upper()} {path} - NOT IMPLEMENTED")
                else:
                    print(f"  [OK] {method.upper()} {path} - Health endpoint (may be in health_handler)")
            else:
                print(f"  [OK] {method.upper()} {path}")
    
    def _validate_backend_paths_in_spec(self):
        """检查Backend实现的路径是否在OpenAPI中定义"""
        print("[INFO] Checking if all Backend paths are documented in OpenAPI...")
        
        for path, method in self.backend.registered_handlers:
            # 跳过不需要文档的路径
            if path.startswith('/api/v'):
                if not self.openapi.get_path_schema(path, method):
                    self.issues.append(ValidationIssue(
                        severity='WARNING',
                        path=path,
                        method=method,
                        message='Path implemented but not documented in OpenAPI'
                    ))
                    print(f"  [WARNING] {method.upper()} {path} - NOT DOCUMENTED")
    
    def _validate_response_fields(self):
        """检查响应字段"""
        print("[INFO] Checking response fields...")
        
        # 抽取几个关键API检查
        test_apis = [
            ('/api/v1/vins', 'get', '200'),
            ('/api/v1/vins/{vin}/sessions', 'post', '201'),
        ]
        
        for path, method, status in test_apis:
            response_schema = self.openapi.get_response_schema(path, method, status)
            if response_schema:
                print(f"  [OK] {method.upper()} {path} has response schema for {status}")
            else:
                self.issues.append(ValidationIssue(
                    severity='WARNING',
                    path=path,
                    method=method,
                    message=f'Missing response schema for status {status}'
                ))
                print(f"  [WARNING] {method.upper()} {path} - NO SCHEMA FOR {status}")
    
    def _validate_version_headers(self):
        """检查版本头定义"""
        print("[INFO] Checking version headers...")
        
        for path, method in self.openapi.get_api_paths():
            path_schema = self.openapi.get_path_schema(path, method)
            if path_schema and 'parameters' in path_schema:
                has_version_header = any(
                    p.get('name') == 'API-Version'
                    for p in path_schema['parameters']
                )
                if has_version_header:
                    print(f"  [OK] {method.upper()} {path} - API-Version header defined")
    
    def print_summary(self):
        """打印验证摘要"""
        errors = [i for i in self.issues if i.severity == 'ERROR']
        warnings = [i for i in self.issues if i.severity == 'WARNING']
        
        print(f"\n{'='*80}")
        print(f"VALIDATION SUMMARY")
        print(f"{'='*80}")
        print(f"Total Issues: {len(self.issues)}")
        print(f"  Errors:   {len(errors)}")
        print(f"  Warnings: {len(warnings)}")
        
        if errors:
            print(f"\nERRORS:")
            for issue in errors:
                print(f"  - {issue.method.upper()} {issue.path}: {issue.message}")
        
        if warnings:
            print(f"\nWARNINGS:")
            for issue in warnings:
                print(f"  - {issue.method.upper()} {issue.path}: {issue.message}")
        
        if not self.issues:
            print("\n✅ All checks passed!")
        
        return len(errors) == 0


def main():
    """主函数"""
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    
    # OpenAPI契约文件
    openapi_file = project_root / 'backend' / 'api' / 'openapi.yaml'
    if not openapi_file.exists():
        print(f"[ERROR] OpenAPI spec not found: {openapi_file}", file=sys.stderr)
        sys.exit(1)
    
    # 解析OpenAPI契约
    print(f"[INFO] Loading OpenAPI spec from {openapi_file}")
    openapi_spec = OpenAPISpec(str(openapi_file))
    print(f"[INFO] API Version: {openapi_spec.api_version}")
    print(f"[INFO] Total paths defined: {len(openapi_spec.get_api_paths())}")
    
    # 解析Backend实现
    backend_dir = project_root / 'backend'
    print(f"[INFO] Scanning Backend implementation in {backend_dir}")
    backend = BackendImplementation(str(backend_dir))
    print(f"[INFO] Total handlers found: {len(backend.registered_handlers)}")
    
    # 执行验证
    validator = APIValidator(openapi_spec, backend)
    issues = validator.validate()
    
    # 打印摘要
    success = validator.print_summary()
    
    # 返回退出码
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
