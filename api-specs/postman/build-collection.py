#!/usr/bin/env python3
"""
Script để build Postman collection từ OpenAPI YAML
Usage: python3 build-collection.py
"""

import os
import sys
import subprocess
import json
from pathlib import Path

# Màu sắc cho output
class Colors:
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    RED = '\033[0;31m'
    NC = '\033[0m'  # No Color

def print_success(msg):
    print(f"{Colors.GREEN}✅ {msg}{Colors.NC}")

def print_warning(msg):
    print(f"{Colors.YELLOW}⚠️  {msg}{Colors.NC}")

def print_error(msg):
    print(f"{Colors.RED}❌ {msg}{Colors.NC}")

def print_info(msg):
    print(f"{Colors.YELLOW}📄 {msg}{Colors.NC}")

def main():
    # Đường dẫn
    script_dir = Path(__file__).parent.absolute()
    api_specs_dir = script_dir.parent
    openapi_file = api_specs_dir / "openapi.yaml"
    output_file = script_dir / "api.collection.json"

    print(f"{Colors.GREEN}🚀 Building Postman collection from OpenAPI spec...{Colors.NC}")

    # Kiểm tra file OpenAPI có tồn tại không
    if not openapi_file.exists():
        print_error(f"OpenAPI file not found at {openapi_file}")
        sys.exit(1)

    print_info(f"OpenAPI file: {openapi_file}")
    print_info(f"Output file: {output_file}")

    # Kiểm tra Node.js và npm
    try:
        subprocess.run(["node", "--version"], check=True, capture_output=True)
        subprocess.run(["npm", "--version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print_error("Node.js or npm is not installed. Please install Node.js first.")
        print_warning("You can install Node.js from: https://nodejs.org/")
        sys.exit(1)

    # Tạo thư mục output nếu chưa có
    output_file.parent.mkdir(parents=True, exist_ok=True)

    # Chuyển đổi OpenAPI sang Postman collection sử dụng npx
    # npx sẽ tự động tải và chạy openapi-to-postmanv2 nếu chưa có
    print(f"{Colors.GREEN}🔄 Converting OpenAPI to Postman collection...{Colors.NC}")
    try:
        result = subprocess.run(
            ["npx", "-y", "openapi-to-postmanv2", "-s", str(openapi_file), "-o", str(output_file)],
            check=True,
            capture_output=True,
            text=True
        )
        
        # Kiểm tra file output
        if output_file.exists():
            file_size = output_file.stat().st_size
            file_size_mb = file_size / (1024 * 1024)
            print_success(f"Postman collection created: {output_file}")
            print_info(f"File size: {file_size_mb:.2f} MB")
            print_success("📥 You can now import this file into Postman")
        else:
            print_error("Output file was not created")
            sys.exit(1)
            
    except subprocess.CalledProcessError as e:
        print_error(f"Conversion failed: {e}")
        if e.stderr:
            print_error(f"Error details: {e.stderr}")
        sys.exit(1)

if __name__ == "__main__":
    main()

