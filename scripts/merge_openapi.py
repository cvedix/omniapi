#!/usr/bin/env python3
"""
Script to merge split OpenAPI files back into a single file.
This is useful after editing individual path files.
"""

import yaml
import os
import sys
from pathlib import Path

def load_yaml(file_path):
    """Load YAML file."""
    with open(file_path, 'r', encoding='utf-8') as f:
        return yaml.safe_load(f)

def save_yaml(file_path, data):
    """Save YAML file with proper formatting."""
    with open(file_path, 'w', encoding='utf-8') as f:
        yaml.dump(data, f, allow_unicode=True, sort_keys=False, default_flow_style=False, width=120)

def merge_openapi_files(base_dir):
    """Merge split OpenAPI files into a single file."""
    base_path = Path(base_dir)
    
    # Load main structure (if exists) or create new
    main_file = base_path / 'openapi.yaml'
    if main_file.exists():
        print(f"Loading main structure from {main_file}...")
        main_data = load_yaml(main_file)
    else:
        print("No main file found. Please run split_openapi.py first.")
        return
    
    # Merge paths from all path files (support both flat and nested structure)
    paths_dir = base_path / 'paths'
    if not paths_dir.exists():
        print(f"Paths directory {paths_dir} not found!")
        return
    
    print("Merging paths from path files...")
    merged_paths = {}
    
    # Check if we have nested structure (paths/<tag>/*.yaml) or flat structure (paths/*.yaml)
    tag_dirs = [d for d in paths_dir.iterdir() if d.is_dir()]
    path_files = []
    
    if tag_dirs:
        # Nested structure: paths/<tag>/*.yaml
        print("  Found nested structure (paths/<tag>/*.yaml)")
        for tag_dir in sorted(tag_dirs):
            tag_files = sorted(tag_dir.glob('*.yaml'))
            path_files.extend(tag_files)
    else:
        # Flat structure: paths/*.yaml
        print("  Found flat structure (paths/*.yaml)")
        path_files = sorted(paths_dir.glob('*.yaml'))
    
    path_count = 0
    for path_file in path_files:
        path_data = load_yaml(path_file)
        
        # Handle both formats:
        # 1. Single path per file: {"/path": {...}}
        # 2. Multiple paths per file: {"paths": {"/path1": {...}, "/path2": {...}}}
        if 'paths' in path_data:
            merged_paths.update(path_data['paths'])
            path_count += len(path_data['paths'])
        else:
            # Single path format - merge directly
            merged_paths.update(path_data)
            path_count += len(path_data)
        
        if path_count % 20 == 0 and path_count > 0:
            print(f"  Processed {path_count} endpoints...")
    
    main_data['paths'] = merged_paths
    print(f"  Merged {path_count} endpoints from {len(path_files)} files")
    
    # Merge components from components files
    components_dir = base_path / 'components'
    if components_dir.exists():
        print("Merging components from component files...")
        schemas_file = components_dir / 'schemas.yaml'
        if schemas_file.exists():
            schemas_data = load_yaml(schemas_file)
            if 'components' in schemas_data and 'schemas' in schemas_data['components']:
                if 'components' not in main_data:
                    main_data['components'] = {}
                if 'schemas' not in main_data['components']:
                    main_data['components']['schemas'] = {}
                main_data['components']['schemas'].update(schemas_data['components']['schemas'])
                print(f"    Added {len(schemas_data['components']['schemas'])} schemas")
    
    # Save merged file
    print(f"Saving merged file to {main_file}...")
    save_yaml(main_file, main_data)
    print(f"Done! Merged {len(merged_paths)} paths into main file.")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python merge_openapi.py <base_dir>")
        print("Example: python merge_openapi.py api-specs/openapi/en")
        sys.exit(1)
    
    base_dir = sys.argv[1]
    
    if not os.path.exists(base_dir):
        print(f"Error: Directory {base_dir} not found!")
        sys.exit(1)
    
    merge_openapi_files(base_dir)
