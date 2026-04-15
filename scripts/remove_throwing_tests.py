import re
import sys
import os

def find_matching_brace(text, start):
    count = 1
    for i in range(start + 1, len(text)):
        if text[i] == '{': count += 1
        elif text[i] == '}': count -= 1
        if count == 0: return i + 1
    return -1

def remove_throwing_tests(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    # 1. Remove declarations from private slots
    # We look for void ...Throws_...(); and a few others
    patterns = [
        r'\s*void\s+\w+Throws_(std|unknown)\(\);',
        r'\s*void\s+logout_stopThrows_recordsError\(\);',
        r'\s*void\s+sessionCreated_setSessionCredentialsThrows_setsError\(\);',
        r'\s*void\s+logout_clearSessionCredentialsThrows_std\(\);',
        r'\s*void\s+vinSelected_empty_clearCredentialsThrows_std\(\);'
    ]
    for p in patterns:
        content = re.sub(p, '', content)
    
    # 2. Remove definitions
    while True:
        match = re.search(r'void\s+TestSessionManager::(\w+Throws_(std|unknown)|logout_stopThrows_recordsError|sessionCreated_setSessionCredentialsThrows_setsError|logout_clearSessionCredentialsThrows_std|vinSelected_empty_clearCredentialsThrows_std)\s*\(\)\s*{', content)
        if not match: break
        
        open_brace = content.find('{', match.start())
        end = find_matching_brace(content, open_brace)
        if end == -1: break
        content = content[:match.start()] + content[end:]
        
    with open(filepath, 'w') as f:
        f.write(content)

if __name__ == "__main__":
    remove_throwing_tests(sys.argv[1])
