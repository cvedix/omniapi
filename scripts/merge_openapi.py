#!/usr/bin/env python3
"""
Script merge OpenAPI tách file và xuất tài liệu.
Tự động phát hiện mọi locale trong api-specs/openapi/ (en, vi, ...); chỉ cần chọn option.

Cách chạy:
  python merge_openapi.py              # hỏi option (1/2/3)
  python merge_openapi.py 1           # chỉ merge openapi.yaml (mọi locale)
  python merge_openapi.py 2           # chỉ sinh API.md (mọi locale)
  python merge_openapi.py 3           # cả hai (1 và 2)

Option:
  1 = Merge paths vào openapi.yaml (từng locale).
  2 = Merge và sinh Markdown vào api-specs/docs/<locale>/API.md.
  3 = Cả hai (1 và 2).
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


def get_merged_spec(base_path):
    """
    Merge split OpenAPI files and return the full spec dict.
    Returns None if base structure is missing.
    """
    base_path = Path(base_path)
    main_file = base_path / 'openapi.yaml'
    if not main_file.exists():
        return None
    main_data = load_yaml(main_file)

    paths_dir = base_path / 'paths'
    if not paths_dir.exists():
        return None

    merged_paths = {}
    tag_dirs = [d for d in paths_dir.iterdir() if d.is_dir()]
    path_files = []
    if tag_dirs:
        for tag_dir in sorted(tag_dirs):
            path_files.extend(sorted(tag_dir.glob('*.yaml')))
    else:
        path_files = sorted(paths_dir.glob('*.yaml'))

    for path_file in path_files:
        path_data = load_yaml(path_file)
        if path_data is None:
            continue
        if 'paths' in path_data:
            merged_paths.update(path_data['paths'])
        else:
            merged_paths.update(path_data)

    main_data['paths'] = merged_paths

    components_dir = base_path / 'components'
    if components_dir.exists():
        schemas_file = components_dir / 'schemas.yaml'
        if schemas_file.exists():
            schemas_data = load_yaml(schemas_file)
            if schemas_data and 'components' in schemas_data and 'schemas' in schemas_data['components']:
                if 'components' not in main_data:
                    main_data['components'] = {}
                if 'schemas' not in main_data['components']:
                    main_data['components']['schemas'] = {}
                main_data['components']['schemas'].update(schemas_data['components']['schemas'])

    return main_data


def merge_openapi_files(base_dir):
    """Merge split OpenAPI files into a single openapi.yaml file."""
    base_path = Path(base_dir)
    main_file = base_path / 'openapi.yaml'
    if not main_file.exists():
        print("No main file found. Please run split_openapi.py first.")
        return
    print(f"Loading main structure from {main_file}...")
    main_data = get_merged_spec(base_dir)
    if main_data is None:
        print("Merge failed (missing paths or main file).")
        return
    paths = main_data.get('paths', {})
    path_count = len(paths)
    op_count = sum(
        1 for methods in paths.values()
        for m in methods if m.lower() in ('get', 'post', 'put', 'patch', 'delete')
    )
    print(f"Merged {path_count} paths, {op_count} endpoint operations.")
    print(f"Saving merged file to {main_file}...")
    save_yaml(main_file, main_data)
    print("Done! Merged paths saved to main file.")


def _schema_to_md(schema, indent=0):
    """Render a schema (or ref) to a short markdown description."""
    if schema is None:
        return ""
    pre = "  " * indent
    if isinstance(schema, dict):
        ref = schema.get('$ref')
        if ref:
            return f"{pre}- `$ref`: {ref}\n"
        stype = schema.get('type', 'object')
        desc = schema.get('description', '')
        out = f"{pre}- type: `{stype}`"
        if desc:
            out += f" — {desc}"
        out += "\n"
        if 'properties' in schema:
            for prop, pval in schema.get('properties', {}).items():
                out += f"{pre}  - **{prop}**: {pval.get('type', '')} — {pval.get('description', '')}\n"
        return out
    return ""


def _content_to_md(content, indent=0):
    """Render request/response content to markdown."""
    if not content:
        return ""
    pre = "  " * indent
    out = ""
    for mt, media in content.items():
        out += f"{pre}- **{mt}**\n"
        if 'schema' in media:
            out += _schema_to_md(media['schema'], indent + 1)
        if 'example' in media:
            out += f"{pre}  - example: `{media['example']}`\n"
    return out


def _responses_to_md(responses):
    """Render responses dict to markdown."""
    if not responses:
        return ""
    out = "| Code | Description |\n|------|-------------|\n"
    for code, r in responses.items():
        desc = (r.get('description') or '').replace('\n', ' ')
        out += f"| {code} | {desc} |\n"
    return out


def openapi_to_markdown(spec):
    """Convert OpenAPI spec dict to a single Markdown document."""
    info = spec.get('info', {})
    title = info.get('title', 'API')
    version = info.get('version', '')
    description = info.get('description', '')
    paths = spec.get('paths', {})

    # Group by tag for readable sections
    by_tag = {}
    for path, path_item in paths.items():
        for method, op in path_item.items():
            if method.lower() in ('get', 'post', 'put', 'patch', 'delete', 'head', 'options'):
                tags = op.get('tags') or ['API']
                for tag in tags:
                    by_tag.setdefault(tag, []).append((path, method.lower(), op))
                    break  # first tag only for grouping

    lines = []
    lines.append(f"# {title}\n")
    if version:
        lines.append(f"**Version:** {version}\n")
    if description:
        lines.append(description.strip() + "\n")
    lines.append("---\n")

    for tag in sorted(by_tag.keys()):
        lines.append(f"## {tag}\n")
        for path, method, op in sorted(by_tag[tag], key=lambda x: (x[0], x[1])):
            summary = op.get('summary', '')
            desc = op.get('description', '')
            op_id = op.get('operationId', '')
            lines.append(f"### `{method.upper()}` {path}\n")
            if summary:
                lines.append(f"**Summary:** {summary}\n")
            if op_id:
                lines.append(f"**Operation ID:** `{op_id}`\n")
            if desc:
                lines.append(desc.strip() + "\n")
            params = op.get('parameters', [])
            if params:
                lines.append("**Parameters:**\n")
                lines.append("| Name | In | Required | Description |\n|------|-----|----------|-------------|\n")
                for p in params:
                    name = p.get('name', '')
                    loc = p.get('in', '')
                    req = 'Yes' if p.get('required') else 'No'
                    d = (p.get('description') or '').replace('\n', ' ')
                    lines.append(f"| {name} | {loc} | {req} | {d} |\n")
            body = op.get('requestBody')
            if body:
                lines.append("**Request body:**\n")
                lines.append(_content_to_md(body.get('content', {})))
            lines.append("**Responses:**\n")
            lines.append(_responses_to_md(op.get('responses', {})))
            lines.append("")
        lines.append("")

    return "\n".join(lines)


def generate_markdown_doc(base_dir):
    """Merge OpenAPI in memory and write a single Markdown file to api-specs/docs/<locale>/API.md."""
    base_path = Path(base_dir)
    locale = base_path.name
    # api-specs/openapi/en -> api-specs/docs/en
    docs_root = base_path.parent.parent / 'docs'
    out_dir = docs_root / locale
    out_file = out_dir / 'API.md'

    spec = get_merged_spec(base_dir)
    if spec is None:
        print("Error: Could not merge spec (missing openapi.yaml or paths).")
        return
    paths = spec.get('paths', {})
    path_count = len(paths)
    op_count = sum(
        1 for methods in paths.values()
        for m in methods if m.lower() in ('get', 'post', 'put', 'patch', 'delete')
    )
    print(f"Merged {path_count} paths, {op_count} endpoint operations.")
    md = openapi_to_markdown(spec)
    out_dir.mkdir(parents=True, exist_ok=True)
    with open(out_file, 'w', encoding='utf-8') as f:
        f.write(md)
    print(f"Done! Markdown document written to {out_file}")


def detect_openapi_locales(project_root):
    """Tìm tất cả thư mục locale trong api-specs/openapi/ (có openapi.yaml và paths/)."""
    openapi_root = Path(project_root) / 'api-specs' / 'openapi'
    if not openapi_root.is_dir():
        return []
    locales = []
    for d in sorted(openapi_root.iterdir()):
        if d.is_dir() and (d / 'openapi.yaml').exists() and (d / 'paths').is_dir():
            locales.append(d)
    return locales


def main():
    # Project root: thư mục chứa api-specs/openapi (từ cwd hoặc từ vị trí script)
    project_root = Path(os.getcwd())
    if not (project_root / 'api-specs' / 'openapi').is_dir():
        script_dir = Path(__file__).resolve().parent
        project_root = script_dir.parent
    if not (project_root / 'api-specs' / 'openapi').is_dir():
        print("Error: Không tìm thấy api-specs/openapi. Chạy script từ thư mục gốc project.")
        sys.exit(1)

    locales = detect_openapi_locales(project_root)
    if not locales:
        print("Error: Không có locale nào (cần openapi.yaml và thư mục paths/ trong api-specs/openapi/<locale>).")
        sys.exit(1)

    locale_names = [d.name for d in locales]
    print(f"Đã phát hiện {len(locales)} locale: {', '.join(locale_names)}")

    # Option: từ tham số dòng lệnh hoặc hỏi người dùng (chỉ hỏi option)
    if len(sys.argv) >= 2 and sys.argv[1].strip() in ('1', '2', '3'):
        option = sys.argv[1].strip()
    else:
        print("\nChọn option:")
        print("  1 = Merge và ghi openapi.yaml (từng locale)")
        print("  2 = Merge và sinh API.md vào api-specs/docs/<locale>/")
        print("  3 = Cả hai (1 và 2)")
        option = input("Option (1/2/3) [1]: ").strip() or "1"

    if option not in ('1', '2', '3'):
        print(f"Unknown option '{option}', dùng 1 (merge YAML).")
        option = "1"

    do_merge_yaml = option in ('1', '3')
    do_markdown = option in ('2', '3')

    for base_path in locales:
        base_dir = str(base_path)
        locale = base_path.name
        print(f"\n--- Locale: {locale} ---")
        if do_merge_yaml:
            merge_openapi_files(base_dir)
        if do_markdown:
            generate_markdown_doc(base_dir)

    print("\nHoàn tất.")


if __name__ == '__main__':
    main()
