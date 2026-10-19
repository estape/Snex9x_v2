# Security Policy

The Snex9x v2 development team takes the security of our users and software seriously. This document outlines our supported versions, the vulnerability reporting process, and our threat model guidelines.

---

## Supported Versions

Only the latest active release and the `main` branch receive official security updates and patches. If you are running an older version, please update to the latest release before submitting a vulnerability report.

| Version | Supported          | Status                                      |
| :------ | :----------------- | :------------------------------------------ |
| 2.x     | :white_check_mark: | Currently supported (active release / main) |
| < 2.0   | :x:                | Unsupported                                 |

---

## Reporting a Vulnerability

Please **DO NOT** report security vulnerabilities through public GitHub issues, discussions, pull requests, or social media.

### Preferred Reporting Method: GitHub Private Vulnerability Reporting

To submit a confidential vulnerability report:

1. Navigate to the **[Security](../../security)** tab of this repository on GitHub.
2. Click on **[Advisories](../../security/advisories)** in the left sidebar.
3. Click the **"Report a vulnerability"** button to open a private advisory draft.
4. Provide a comprehensive summary of the vulnerability:
   - **Type of issue**: (e.g., buffer overflow, memory corruption, IPC injection, path traversal).
   - **Impact**: What an attacker could achieve if the vulnerability is exploited.
   - **Affected component**: Specific module (e.g., ROM loader, Snes9x PPU core, Photino WebMessage bridge, BoxartService).
   - **Reproduction steps / Proof of Concept (PoC)**: Detailed steps or minimal non-copyrighted reproducible data.
   - **Proposed remediation**: Any suggested patch or workaround, if available.

### What to Expect After Submission

- **Acknowledgment**: We aim to review and acknowledge receipt of your vulnerability report within **48 to 72 hours**.
- **Assessment**: We will validate the issue, confirm its severity rating (CVSS score), and keep you informed of our progress.
- **Remediation & Fix**: We strive to develop, verify, and release a security patch within a **30 to 90-day** coordinated disclosure window.
- **Credit & Attribution**: Once the fix is published and a new version is released, you will be publicly credited in the security advisory and release notes (unless you request anonymity).

---

## Threat Model & Scope

### In-Scope Vulnerabilities

We welcome reports regarding security issues that impact the confidentiality, integrity, or availability of the application and the host operating system:

1. **Malicious ROM File Parsing**:
   - Memory safety issues (heap/stack buffer overflows, integer overflows, use-after-free, out-of-bounds writes) triggered by parsing maliciously crafted SNES ROM headers, SMC/copier tags, or compressed archives.
   - Remote Code Execution (RCE) or denial of service through malformed ROM data.
2. **WebView2 & Photino IPC Boundaries**:
   - Cross-Site Scripting (XSS) or arbitrary code execution originating from untrusted web content or local file protocol exploitation.
   - Injection vulnerabilities in the `window.chrome.webview.postMessage` / `RegisterWebMessageReceivedHandler` IPC bridge.
   - Inter-Process Communication (IPC) shared memory buffer corruption or unauthorized memory mapping.
3. **Network & Boxart Downloader (`BoxartService`)**:
   - Path traversal or arbitrary file write vulnerabilities when saving thumbnail covers to the local cache directory (`config/boxart/`).
   - Insecure deserialization or buffer handling during WinHTTP HTTPS downloads from external repositories (`libretro-thumbnails`).
4. **Configuration & Save State Persistence**:
   - Arbitrary code execution or privilege escalation via corrupted save state files (`.frz`, `.s01`..`.s10`) or configuration tampering.

---

### Out-of-Scope Vulnerabilities

The following scenarios are considered out of scope and will generally be closed without action:

- Vulnerabilities requiring physical access, device theft, or local administrative root compromise of the user's computer.
- Attacks requiring the user to attach a debugger, modify memory at runtime with external tools (e.g., Cheat Engine), or manually alter binary DLLs.
- Denial of Service (crashes) caused solely by running known broken/corrupt homebrew ROMs without security impact beyond crashing the emulator process.
- Upstream vulnerabilities in third-party libraries (e.g., Chromium / Microsoft WebView2, SDL3, or unmodified upstream Snes9x core) unless a viable exploit vector exists specifically through Snex9x v2.
- Social engineering, phishing, or attacks targeting project maintainers.

---

## Responsible Disclosure & Copyright Rules

> [!CAUTION]
> **DO NOT attach or distribute copyrighted commercial ROM files.**

- **No Pirated ROMs**: When submitting a Proof of Concept (PoC) demonstrating a ROM parser exploit, **do not** attach copyrighted commercial game dumps.
- **Minimal PoCs**: Please use custom open-source homebrew test ROMs, synthetic byte sequences, or minimal hex patch snippets.
- **Coordinated Disclosure**: We ask that you respect responsible disclosure principles and refrain from disclosing the vulnerability to the public or third parties until an official security advisory and patch have been released.

Thank you for helping keep Snex9x v2 and its users safe!

