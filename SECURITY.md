# Security Policy

## Supported Versions

| Version | Supported |
| ------- | --------- |
| 3.1.x   | Yes       |
| < 3.1   | No        |

## Security Model
- **Standard-User Default (`AsInvoker`)**: EverythingVS launches without requiring elevation. Administrator privileges are requested only when explicitly triggered via **Tools -> Run as Administrator (Fast NTFS MFT)**.
- **Localhost-Only HTTP Server (Off by Default)**: The built-in HTTP/JSON search server is disabled by default (`allow_http_server=0`) and binds strictly to `127.0.0.1` (`INADDR_LOOPBACK`) with socket timeouts and `X-Content-Type-Options: nosniff` / `X-Frame-Options: DENY` headers.
- **Cache Validation**: `Everything.db` is bounds-checked on load to guard against corrupted or tampered index caches.

## Reporting a Vulnerability
Please report security issues privately through GitHub Security Advisories or by opening a minimal contact issue without posting exploit details publicly.
