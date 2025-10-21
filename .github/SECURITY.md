# Security Policy

**"Sets wallpapers until it... doesn't (securely)"**

## Supported Versions

We actively maintain and provide security updates for the following versions:

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |
| < 0.1   | :x:                |

## Reporting a Vulnerability

We take security seriously. If you discover a security vulnerability in Staticwall, please report it responsibly.

### How to Report

**DO NOT** open a public GitHub issue for security vulnerabilities.

Instead, please:

1. **Email:** Send details to the maintainer (check GitHub profile for contact)
2. **GitHub Security Advisory:** Use the [private vulnerability reporting feature](https://github.com/1ay1/staticwall/security/advisories/new)

### What to Include

Please provide:

- Description of the vulnerability
- Steps to reproduce the issue
- Affected versions
- Potential impact
- Any suggested fixes (if you have them)

### Response Timeline

- **Initial Response:** Within 48 hours
- **Status Update:** Within 7 days
- **Fix Timeline:** Depends on severity
  - Critical: 1-3 days
  - High: 1-2 weeks
  - Medium: 2-4 weeks
  - Low: Next release cycle

### Security Considerations

Staticwall is designed with security in mind:

- **No Root Privileges:** Runs as a normal user
- **No Network Access:** Purely local, no external connections
- **Limited Attack Surface:** Minimal dependencies
- **Input Validation:** All user inputs are validated
- **Safe File Operations:** Proper bounds checking on file I/O
- **Memory Safety:** Careful memory management with proper cleanup

### Known Security Boundaries

What Staticwall does NOT protect against:

- **Malicious Images:** We load PNG/JPEG files - ensure your image sources are trusted
- **Config Injection:** Don't allow untrusted users to modify your config file
- **Symlink Attacks:** Be cautious with symbolic links in wallpaper directories
- **File Permissions:** Ensure your wallpaper files have appropriate permissions

### Security Best Practices

When using Staticwall:

1. ✅ Keep Staticwall updated to the latest version
2. ✅ Use wallpapers from trusted sources
3. ✅ Set appropriate file permissions on config files (`chmod 600 ~/.config/staticwall/config.vibe`)
4. ✅ Verify image files before using them as wallpapers
5. ✅ Don't run Staticwall with elevated privileges (unnecessary and dangerous)
6. ✅ Review config changes if using auto-reload (`--watch`)

### Past Security Issues

None reported yet. We'll maintain a list here if any are discovered.

### Acknowledgments

We appreciate responsible disclosure and will credit security researchers who report vulnerabilities (unless they prefer to remain anonymous).

---

**Remember:** Even wallpaper daemons deserve secure code. If you find something, let us know!