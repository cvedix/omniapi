#!/usr/bin/env python3
"""
Automated test script for creating and starting BA Loitering instances.
This script creates multiple instances and starts them automatically.
"""

import requests
import json
import sys
import time
from typing import Dict, List, Optional

# API base URL
BASE_URL = "http://localhost:8080"
CREATE_INSTANCE_URL = f"{BASE_URL}/v1/core/instance"
START_INSTANCE_URL_TEMPLATE = f"{BASE_URL}/v1/core/instance/{{instance_id}}/start"

# Request body template
INSTANCE_BODY = {
    "name": "ba_loitering_rtmp",
    "group": "demo",
    "solution": "ba_loitering",
    "autoStart": False,
    "additionalParams": {
        "input": {
            "RTMP_SRC_URL": "rtmp://192.168.1.128:1935/live/camera_demo_sang_vehicle",
            "WEIGHTS_PATH": "/opt/edge_ai_api/models/det_cls/yolov3-tiny-2022-0721_best.weights",
            "CONFIG_PATH": "/opt/edge_ai_api/models/det_cls/yolov3-tiny-2022-0721.cfg",
            "LABELS_PATH": "/opt/edge_ai_api/models/det_cls/yolov3_tiny_5classes.txt",
            "RESIZE_RATIO": "1.0",
            "GST_DECODER_NAME": "nvh264dec"
        },
        "output": {
            "RTMP_DES_URL": "rtmp://192.168.1.128:1935/live/ba_loitering_stream_1",
            "ENABLE_SCREEN_DES": "false"
        },
        "LOITERING_AREAS_JSON": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":600,\"y\":30},{\"x\":600,\"y\":300},{\"x\":20,\"y\":300}],\"seconds\":5.0,\"channel\":0}]",
        "ALARM_SECONDS": "5"
    }
}


def create_instance(instance_number: int) -> Optional[Dict]:
    """
    Create a new instance via API.
    
    Args:
        instance_number: The instance number (for logging purposes)
    
    Returns:
        Response JSON if successful, None otherwise
    """
    try:
        print(f"\n[{instance_number}] Creating instance...")
        # Increased timeout to 60 seconds because pipeline building can take 30+ seconds
        # Pipeline building is synchronous and blocks the HTTP response
        response = requests.post(
            CREATE_INSTANCE_URL,
            json=INSTANCE_BODY,
            headers={"Content-Type": "application/json"},
            timeout=60
        )
        
        if response.status_code == 200 or response.status_code == 201:
            data = response.json()
            instance_id = data.get("instanceId")
            print(f"[{instance_number}] ✓ Instance created successfully")
            print(f"[{instance_number}]   Instance ID: {instance_id}")
            print(f"[{instance_number}]   Display Name: {data.get('displayName', 'N/A')}")
            return data
        else:
            print(f"[{instance_number}] ✗ Failed to create instance")
            print(f"[{instance_number}]   Status Code: {response.status_code}")
            print(f"[{instance_number}]   Response: {response.text}")
            return None
            
    except requests.exceptions.RequestException as e:
        print(f"[{instance_number}] ✗ Error creating instance: {str(e)}")
        return None


def start_instance(instance_id: str, instance_number: int) -> bool:
    """
    Start an instance via API.
    
    Args:
        instance_id: The instance ID to start
        instance_number: The instance number (for logging purposes)
    
    Returns:
        True if successful, False otherwise
    """
    try:
        url = START_INSTANCE_URL_TEMPLATE.format(instance_id=instance_id)
        print(f"[{instance_number}] Starting instance {instance_id}...")
        
        # Increased timeout to 60 seconds to allow for pipeline operations
        response = requests.post(
            url,
            headers={"Content-Type": "application/json"},
            timeout=60
        )
        
        if response.status_code == 200 or response.status_code == 201:
            print(f"[{instance_number}] ✓ Instance started successfully")
            return True
        else:
            print(f"[{instance_number}] ✗ Failed to start instance")
            print(f"[{instance_number}]   Status Code: {response.status_code}")
            print(f"[{instance_number}]   Response: {response.text}")
            return False
            
    except requests.exceptions.RequestException as e:
        print(f"[{instance_number}] ✗ Error starting instance: {str(e)}")
        return False


def test_instances(num_instances: int) -> Dict[str, List]:
    """
    Create and start multiple instances.
    
    Args:
        num_instances: Number of instances to create and start
    
    Returns:
        Dictionary with 'created' and 'started' lists of instance IDs
    """
    results = {
        "created": [],
        "started": [],
        "failed_creation": [],
        "failed_start": []
    }
    
    print(f"\n{'='*60}")
    print(f"Starting test: Creating and starting {num_instances} instance(s)")
    print(f"{'='*60}")
    
    # Create instances
    for i in range(1, num_instances + 1):
        instance_data = create_instance(i)
        
        if instance_data:
            instance_id = instance_data.get("instanceId")
            if instance_id:
                results["created"].append({
                    "instance_id": instance_id,
                    "instance_number": i,
                    "data": instance_data
                })
            else:
                results["failed_creation"].append(i)
        else:
            results["failed_creation"].append(i)
        
        # Small delay between requests
        time.sleep(0.5)
    
    print(f"\n{'='*60}")
    print(f"Creation phase complete: {len(results['created'])}/{num_instances} successful")
    print(f"{'='*60}")
    
    # Start instances
    if results["created"]:
        print(f"\n{'='*60}")
        print(f"Starting {len(results['created'])} instance(s)...")
        print(f"{'='*60}")
        
        for instance_info in results["created"]:
            instance_id = instance_info["instance_id"]
            instance_number = instance_info["instance_number"]
            
            if start_instance(instance_id, instance_number):
                results["started"].append(instance_id)
            else:
                results["failed_start"].append(instance_id)
            
            # Small delay between requests
            time.sleep(0.5)
    
    return results


def print_summary(results: Dict[str, List]):
    """Print test summary."""
    print(f"\n{'='*60}")
    print("TEST SUMMARY")
    print(f"{'='*60}")
    print(f"Total instances requested: {len(results['created']) + len(results['failed_creation'])}")
    print(f"Successfully created: {len(results['created'])}")
    print(f"Failed to create: {len(results['failed_creation'])}")
    print(f"Successfully started: {len(results['started'])}")
    print(f"Failed to start: {len(results['failed_start'])}")
    
    if results["created"]:
        print(f"\nCreated Instance IDs:")
        for instance_info in results["created"]:
            print(f"  [{instance_info['instance_number']}] {instance_info['instance_id']}")
    
    if results["started"]:
        print(f"\nStarted Instance IDs:")
        for instance_id in results["started"]:
            print(f"  - {instance_id}")
    
    if results["failed_creation"]:
        print(f"\nFailed to create instances:")
        for num in results["failed_creation"]:
            print(f"  - Instance #{num}")
    
    if results["failed_start"]:
        print(f"\nFailed to start instances:")
        for instance_id in results["failed_start"]:
            print(f"  - {instance_id}")
    
    print(f"{'='*60}\n")


def main():
    """Main function."""
    print("\n" + "="*60)
    print("BA Loitering Instance Creation and Start Test Script")
    print("="*60)
    
    # Get number of instances from user
    try:
        num_instances_input = input("\nEnter the number of instances to test: ").strip()
        num_instances = int(num_instances_input)
        
        if num_instances <= 0:
            print("Error: Number of instances must be greater than 0")
            sys.exit(1)
        
        if num_instances > 100:
            confirm = input(f"Warning: You are about to create {num_instances} instances. Continue? (y/n): ")
            if confirm.lower() != 'y':
                print("Test cancelled.")
                sys.exit(0)
        
    except ValueError:
        print("Error: Please enter a valid number")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n\nTest cancelled by user.")
        sys.exit(0)
    
    # Run the test
    try:
        results = test_instances(num_instances)
        print_summary(results)
        
        # Exit with appropriate code
        if len(results["failed_creation"]) > 0 or len(results["failed_start"]) > 0:
            sys.exit(1)
        else:
            sys.exit(0)
            
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user.")
        sys.exit(1)
    except Exception as e:
        print(f"\nUnexpected error: {str(e)}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
