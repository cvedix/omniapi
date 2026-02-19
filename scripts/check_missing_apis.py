#!/usr/bin/env python3
"""
Script to check for missing API documentation in OpenAPI spec.
Compares endpoints defined in code with paths in OpenAPI spec.
"""

import yaml
import re
import os
import sys
from pathlib import Path
from collections import defaultdict

def load_yaml(file_path):
    """Load YAML file."""
    with open(file_path, 'r', encoding='utf-8') as f:
        return yaml.safe_load(f)

def extract_endpoints_from_code():
    """Extract all endpoints from C++ handler files."""
    include_dir = Path('include/api')
    endpoints = set()
    
    # Pattern to match ADD_METHOD_TO lines
    pattern = re.compile(r'ADD_METHOD_TO\s*\([^,]+,\s*"([^"]+)"', re.MULTILINE)
    
    for header_file in include_dir.glob('*.h'):
        # Skip swagger and scalar handlers (internal endpoints)
        if header_file.name in ['swagger_handler.h', 'scalar_handler.h']:
            continue
            
        try:
            content = header_file.read_text(encoding='utf-8')
            matches = pattern.findall(content)
            for match in matches:
                endpoints.add(match)
        except Exception as e:
            print(f"Warning: Could not read {header_file}: {e}")
    
    return endpoints

def extract_paths_from_openapi(openapi_file):
    """Extract all paths from OpenAPI spec."""
    try:
        data = load_yaml(openapi_file)
        return set(data.get('paths', {}).keys())
    except Exception as e:
        print(f"Error loading OpenAPI file {openapi_file}: {e}")
        return set()

def normalize_path(path):
    """Normalize path for comparison (remove trailing slashes, etc)."""
    return path.rstrip('/')

def check_missing_apis():
    """Check for missing APIs in OpenAPI spec."""
    print("=" * 80)
    print("Checking for missing API documentation...")
    print("=" * 80)
    
    # Extract endpoints from code
    print("\n1. Extracting endpoints from code...")
    code_endpoints = extract_endpoints_from_code()
    print(f"   Found {len(code_endpoints)} endpoints in code")
    
    # Extract paths from OpenAPI spec (English)
    print("\n2. Extracting paths from OpenAPI spec (English)...")
    en_openapi = Path('api-specs/openapi/en/openapi.yaml')
    if not en_openapi.exists():
        print(f"   Error: {en_openapi} not found!")
        return
    
    openapi_paths = extract_paths_from_openapi(en_openapi)
    print(f"   Found {len(openapi_paths)} paths in OpenAPI spec")
    
    # Normalize paths for comparison
    code_endpoints_norm = {normalize_path(e) for e in code_endpoints}
    openapi_paths_norm = {normalize_path(p) for p in openapi_paths}
    
    # Find missing endpoints
    print("\n3. Comparing endpoints...")
    missing_in_spec = code_endpoints_norm - openapi_paths_norm
    extra_in_spec = openapi_paths_norm - code_endpoints_norm
    
    # Group missing endpoints by handler
    missing_by_handler = defaultdict(list)
    for endpoint in missing_in_spec:
        # Try to determine handler from path
        if '/core/instance' in endpoint:
            handler = 'InstanceHandler'
        elif '/securt/instance' in endpoint:
            handler = 'SecuRTHandler'
        elif '/core/node' in endpoint:
            handler = 'NodeHandler'
        elif '/core/solution' in endpoint:
            handler = 'SolutionHandler'
        elif '/core/group' in endpoint:
            handler = 'GroupHandler'
        elif '/core/ai' in endpoint:
            handler = 'AIHandler'
        elif '/core/config' in endpoint:
            handler = 'ConfigHandler'
        elif '/core/log' in endpoint:
            handler = 'LogHandler'
        elif '/onvif' in endpoint:
            handler = 'ONVIFHandler'
        elif '/core/model' in endpoint:
            handler = 'ModelHandler'
        elif '/core/video' in endpoint:
            handler = 'VideoHandler'
        elif '/core/font' in endpoint:
            handler = 'FontHandler'
        elif '/core/line' in endpoint or '/core/instance' in endpoint and '/line' in endpoint:
            handler = 'LinesHandler'
        elif '/core/area' in endpoint or '/core/instance' in endpoint and ('/jam' in endpoint or '/stop' in endpoint):
            handler = 'AreaHandler'
        elif '/securt/line' in endpoint:
            handler = 'SecuRTLineHandler'
        elif '/securt/area' in endpoint:
            handler = 'SecuRTAreaHandler'
        else:
            handler = 'Unknown'
        missing_by_handler[handler].append(endpoint)
    
    # Print results
    print("\n" + "=" * 80)
    print("RESULTS")
    print("=" * 80)
    
    if missing_in_spec:
        print(f"\n❌ MISSING IN OPENAPI SPEC ({len(missing_in_spec)} endpoints):")
        print("-" * 80)
        for handler in sorted(missing_by_handler.keys()):
            print(f"\n  {handler}:")
            for endpoint in sorted(missing_by_handler[handler]):
                print(f"    - {endpoint}")
    else:
        print("\n✅ All endpoints from code are documented in OpenAPI spec!")
    
    if extra_in_spec:
        print(f"\n⚠️  EXTRA IN OPENAPI SPEC ({len(extra_in_spec)} paths):")
        print("    (These paths exist in spec but not in code - might be deprecated)")
        print("-" * 80)
        for path in sorted(extra_in_spec):
            print(f"    - {path}")
    
    # Summary
    print("\n" + "=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Total endpoints in code: {len(code_endpoints_norm)}")
    print(f"Total paths in OpenAPI spec: {len(openapi_paths_norm)}")
    print(f"Missing in spec: {len(missing_in_spec)}")
    print(f"Extra in spec: {len(extra_in_spec)}")
    print(f"Coverage: {((len(code_endpoints_norm) - len(missing_in_spec)) / len(code_endpoints_norm) * 100):.1f}%")
    
    if missing_in_spec:
        print("\n⚠️  Action required: Add missing endpoints to OpenAPI spec!")
        return 1
    else:
        print("\n✅ All endpoints are documented!")
        return 0

if __name__ == '__main__':
    os.chdir(Path(__file__).parent.parent)
    sys.exit(check_missing_apis())
