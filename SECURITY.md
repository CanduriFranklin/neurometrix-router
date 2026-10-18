# SECURITY & PRIVACY POLICY

## 1. Zero PII Leakage (HIPAA/GDPR Compliance)
Neurometrix is designed with privacy-by-design. 
* **Data Lake (Local):** DICOM images are processed locally by the C++ core. 
* **LLM API:** Only anonymized mathematical metrics (CBF, HU, etc.) are sent to the Google Gemma API. **No patient images or identifiable metadata are ever transmitted to external LLM providers.**

## 2. Human-in-the-Loop (HITL) Safety
AI hallucinations in medicine can be fatal. Neurometrix enforces a strict HITL protocol:
* The AI cannot finalize a report.
* A licensed physician must review and digitally sign (Approve/Reject) every case in the UI.
* All decisions are logged in the \medical_reviews\ PostgreSQL table for legal auditability.

## 3. Responsible Disclosure
If you discover a security vulnerability, please contact the author directly. Do not open public issues regarding security flaws.
