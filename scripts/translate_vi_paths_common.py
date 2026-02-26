#!/usr/bin/env python3
"""Replace common English phrases with Vietnamese in api-specs/openapi/vi/paths/**/*.yaml.
Run from project root. Safe to run multiple times (idempotent for already-translated text).
"""
import os
from pathlib import Path

REPLACEMENTS = [
    ("description: Instance ID\n", "description: Định danh instance\n"),
    ("description: Instance not found\n", "description: Không tìm thấy instance\n"),
    ("description: Invalid request\n", "description: Yêu cầu không hợp lệ\n"),
    ("description: Internal server error\n", "description: Lỗi máy chủ\n"),
    ("description: Server error\n", "description: Lỗi máy chủ\n"),
    ("description: CORS preflight response\n", "description: Phản hồi CORS preflight\n"),
    ("description: Area ID\n", "description: Định danh vùng (area ID)\n"),
    ("description: The unique identifier of the instance\n", "description: Định danh duy nhất của instance\n"),
    ("description: The unique identifier of the line to retrieve\n", "description: Định danh duy nhất của đường cần lấy\n"),
    ("description: The unique identifier of the line to update\n", "description: Định danh duy nhất của đường cần cập nhật\n"),
    ("description: The unique identifier of the line to delete\n", "description: Định danh duy nhất của đường cần xóa\n"),
    ("description: List of crossing lines\n", "description: Danh sách đường vượt (crossing lines)\n"),
    ("description: All lines deleted successfully\n", "description: Đã xóa tất cả lines thành công\n"),
    ("description: List of font files\n", "description: Danh sách file font\n"),
    ("description: List of video files\n", "description: Danh sách file video\n"),
    ("description: List of instances\n", "description: Danh sách instance\n"),
    ("description: List of supported classes\n", "description: Danh sách class được hỗ trợ\n"),
    ("description: List of face subjects\n", "description: Danh sách face subject\n"),
    ("description: List of face subjects retrieved successfully\n", "description: Đã lấy danh sách face subject thành công\n"),
    ("description: All face subjects deleted successfully\n", "description: Đã xóa tất cả face subject thành công\n"),
    ("description: List of exclusion areas\n", "description: Danh sách vùng loại trừ (exclusion areas)\n"),
    ("description: All exclusion areas deleted successfully\n", "description: Đã xóa tất cả vùng loại trừ thành công\n"),
    ("description: All lines deleted successfully\n", "description: Đã xóa tất cả lines thành công\n"),
    ("description: List of templates retrieved successfully\n", "description: Đã lấy danh sách template thành công\n"),
    ("description: List of solutions\n", "description: Danh sách solution\n"),
    ("description: List of nodes retrieved successfully\n", "description: Đã lấy danh sách node thành công\n"),
    ("description: Delete all instances operation completed\n", "description: Đã hoàn thành thao tác xóa tất cả instance\n"),
    ("description: Failed to restart instance\n", "description: Khởi động lại instance thất bại\n"),
    ("description: Failed to stop instance\n", "description: Dừng instance thất bại\n"),
    ("description: Failed to start instance\n", "description: Khởi động instance thất bại\n"),
    ("description: Successfully configured stream output\n", "description: Đã cấu hình stream output thành công\n"),
    ("description: Invalid request (invalid output type or missing required fields)\n", "description: Yêu cầu không hợp lệ (loại output sai hoặc thiếu trường bắt buộc)\n"),
    ("description: Invalid request (invalid input type or missing required fields)\n", "description: Yêu cầu không hợp lệ (loại input sai hoặc thiếu trường bắt buộc)\n"),
    ("description: Invalid request or validation failed\n", "description: Yêu cầu không hợp lệ hoặc validation thất bại\n"),
    ("description: Invalid request (missing file, invalid image format, etc.)\n", "description: Yêu cầu không hợp lệ (thiếu file, định dạng ảnh không hợp lệ, v.v.)\n"),
    # Với thụt lề (parameter/response trong path)
    ("      description: The unique identifier of the instance\n", "      description: Định danh duy nhất của instance\n"),
    ("      description: The unique identifier of the line to retrieve\n", "      description: Định danh duy nhất của đường cần lấy\n"),
    ("      description: The unique identifier of the line to update\n", "      description: Định danh duy nhất của đường cần cập nhật\n"),
    ("      description: The unique identifier of the line to delete\n", "      description: Định danh duy nhất của đường cần xóa\n"),
    ("        description: List of crossing lines\n", "        description: Danh sách đường vượt (crossing lines)\n"),
    ("        description: Line retrieved successfully\n", "        description: Đã lấy đường thành công\n"),
    ("        description: Instance or line not found\n", "        description: Không tìm thấy instance hoặc đường\n"),
    ("        description: Line updated successfully\n", "        description: Đã cập nhật đường thành công\n"),
    ("        description: Bad request (invalid coordinates or parameters)\n", "        description: Yêu cầu không hợp lệ (tọa độ hoặc tham số sai)\n"),
    ("        description: All lines deleted successfully\n", "        description: Đã xóa tất cả đường thành công\n"),
]

def main():
    base = Path("api-specs/openapi/vi/paths")
    if not base.exists():
        print("Not found:", base.resolve())
        return
    count_files = 0
    for yaml_file in base.rglob("*.yaml"):
        try:
            raw = yaml_file.read_text(encoding="utf-8")
            new_raw = raw
            for old, new in REPLACEMENTS:
                new_raw = new_raw.replace(old, new)
            if new_raw != raw:
                yaml_file.write_text(new_raw, encoding="utf-8")
                count_files += 1
        except Exception as e:
            print("Error", yaml_file, e)
    print(f"Updated {count_files} files with common VI replacements.")

if __name__ == "__main__":
    main()
