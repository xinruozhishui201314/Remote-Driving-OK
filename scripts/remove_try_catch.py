import sys
import re
import os

def find_matching_brace(content, start_index):
    brace_count = 1
    i = start_index + 1
    while i < len(content) and brace_count > 0:
        if content[i] == '{':
            brace_count += 1
        elif content[i] == '}':
            brace_count -= 1
        i += 1
    if brace_count == 0:
        return i
    return -1

def remove_try_catch_from_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    # 1. Replace 'try {' with '{'
    # Use a regex that handles whitespace
    content = re.sub(r'\btry\s*{', '{', content)
    
    # 2. Repeatedly find 'catch' and remove its block
    while True:
        # Find the first 'catch' that is not in a comment or string (simplified)
        # We look for \bcatch\b followed by optional (expr) and then {
        match = re.search(r'\bcatch\s*(\([^)]*\))?\s*{', content)
        if not match:
            break
        
        start_index = match.start()
        open_brace_index = content.find('{', start_index)
        
        end_index = find_matching_brace(content, open_brace_index)
        if end_index == -1:
            print(f"Warning: Could not find matching brace for catch in {filepath} at {start_index}")
            break
        
        # Remove from 'catch' to the end of its block
        content = content[:start_index] + content[end_index:]
    
    with open(filepath, 'w') as f:
        f.write(content)

if __name__ == "__main__":
    for arg in sys.argv[1:]:
        if os.path.isfile(arg):
            print(f"Processing {arg}...")
            remove_try_catch_from_file(arg)
        elif os.path.isdir(arg):
            for root, dirs, files in os.walk(arg):
                for file in files:
                    if file.endswith(('.cpp', '.h', '.hpp', '.cc')):
                        fullpath = os.path.join(root, file)
                        print(f"Processing {fullpath}...")
                        remove_try_catch_from_file(fullpath)
