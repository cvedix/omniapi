#!/usr/bin/env python3
"""
Script to split large OpenAPI YAML files into smaller, manageable files.
Each API endpoint will be split into a separate file.
"""

import yaml
import os
import sys
import re
from pathlib import Path
from collections import defaultdict

def load_yaml(file_path):
    """Load YAML file."""
    with open(file_path, 'r', encoding='utf-8') as f:
        return yaml.safe_load(f)

def save_yaml(file_path, data):
    """Save YAML file with proper formatting."""
    with open(file_path, 'w', encoding='utf-8') as f:
        yaml.dump(data, f, allow_unicode=True, sort_keys=False, default_flow_style=False, width=120)

def sanitize_filename(name):
    """Sanitize string to be used as filename."""
    # Replace special characters with underscores
    name = re.sub(r'[^\w\-_]', '_', name)
    # Replace multiple underscores with single
    name = re.sub(r'_+', '_', name)
    # Remove leading/trailing underscores
    name = name.strip('_')
    return name.lower()

def get_path_filename(path):
    """Generate filename from API path."""
    # Remove leading /v1/ or /v2/ etc
    clean_path = re.sub(r'^/v\d+/', '/', path)
    # Replace slashes and special chars with underscores
    filename = clean_path.replace('/', '_').replace('{', '').replace('}', '')
    # Remove leading underscore if exists
    filename = filename.lstrip('_')
    return sanitize_filename(filename) if filename else 'root'

def get_primary_tag(path_item):
    """Get primary tag for a path item."""
    for method, operation in path_item.items():
        if method in ['get', 'post', 'put', 'patch', 'delete', 'options', 'head']:
            if isinstance(operation, dict) and 'tags' in operation:
                # Use the first tag as primary
                return operation['tags'][0] if operation['tags'] else 'Other'
    return 'Other'

def split_openapi_file(input_file, output_dir, lang='en'):
    """Split OpenAPI file into smaller files - one file per API endpoint."""
    print(f"Loading {input_file}...")
    openapi_data = load_yaml(input_file)
    
    # Backup original file
    backup_file = Path(input_file).parent / f'openapi.yaml.backup'
    print(f"Creating backup: {backup_file}")
    with open(backup_file, 'w', encoding='utf-8') as f:
        yaml.dump(openapi_data, f, allow_unicode=True, sort_keys=False, default_flow_style=False, width=120)
    
    # Create output directories
    paths_dir = Path(output_dir) / 'paths'
    components_dir = Path(output_dir) / 'components'
    paths_dir.mkdir(parents=True, exist_ok=True)
    components_dir.mkdir(parents=True, exist_ok=True)
    
    # Extract and save each path as a separate file
    print("Extracting paths - one file per endpoint...")
    path_count = 0
    paths_by_tag = defaultdict(list)
    
    for path, path_item in openapi_data.get('paths', {}).items():
        # Get primary tag for organizing into subdirectories
        primary_tag = get_primary_tag(path_item)
        tag_dir = paths_dir / sanitize_filename(primary_tag)
        tag_dir.mkdir(parents=True, exist_ok=True)
        
        # Generate filename from path
        filename = get_path_filename(path)
        path_file = tag_dir / f'{filename}.yaml'
        
        # Save single path
        with open(path_file, 'w', encoding='utf-8') as f:
            f.write(f'# API endpoint: {path}\n')
            f.write(f'# Tag: {primary_tag}\n\n')
            yaml.dump({path: path_item}, f, allow_unicode=True, sort_keys=False, default_flow_style=False, width=120)
        
        paths_by_tag[primary_tag].append(path)
        path_count += 1
        
        if path_count % 10 == 0:
            print(f"  Processed {path_count} endpoints...")
    
    print(f"  Created {path_count} endpoint files in {len(paths_by_tag)} tag directories")
    
    # Extract and save components
    print("Extracting components...")
    if 'components' in openapi_data:
        components_file = components_dir / 'schemas.yaml'
        # Save schemas
        schemas_data = openapi_data['components'].get('schemas', {})
        with open(components_file, 'w', encoding='utf-8') as f:
            f.write('# Component schemas\n')
            yaml.dump({'components': {'schemas': schemas_data}}, f, allow_unicode=True, sort_keys=False, default_flow_style=False, width=120)
        print(f"  Created {components_file} with {len(schemas_data)} schemas")
    
    # Create main openapi.yaml that merges all paths
    print("Creating main openapi.yaml that merges all paths...")
    main_openapi = {
        'openapi': openapi_data.get('openapi', '3.0.3'),
        'info': openapi_data.get('info', {}),
        'servers': openapi_data.get('servers', []),
        'paths': {}
    }
    
    # Merge all paths from original data
    main_openapi['paths'] = openapi_data.get('paths', {})
    
    # Add components
    if 'components' in openapi_data:
        main_openapi['components'] = openapi_data['components']
    
    # Add tags and x-tagGroups if they exist
    if 'tags' in openapi_data:
        main_openapi['tags'] = openapi_data['tags']
    if 'x-tagGroups' in openapi_data:
        main_openapi['x-tagGroups'] = openapi_data['x-tagGroups']
    
    # Save main file
    main_file = Path(output_dir) / 'openapi.yaml'
    save_yaml(main_file, main_openapi)
    print(f"  Created {main_file} (merged from {path_count} endpoint files)")
    
    print(f"\nDone! Split {path_count} endpoints into separate files.")
    print(f"Main file {main_file} contains merged paths for Swagger/Scalar compatibility.")
    print(f"\nTo edit an endpoint, modify files in {paths_dir}/<tag>/")
    print(f"To edit schemas, modify {components_dir}/schemas.yaml")
    print(f"Then run: python scripts/merge_openapi.py {output_dir} to regenerate main file.")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python split_openapi.py <input_file> <output_dir> [lang]")
        print("Example: python split_openapi.py api-specs/openapi/en/openapi.yaml api-specs/openapi/en en")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_dir = sys.argv[2]
    lang = sys.argv[3] if len(sys.argv) > 3 else 'en'
    
    if not os.path.exists(input_file):
        print(f"Error: File {input_file} not found!")
        sys.exit(1)
    
    split_openapi_file(input_file, output_dir, lang)
