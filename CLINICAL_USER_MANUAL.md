# Neurometrix: Clinical User Guide
**For Radiologists, Neurologists, and Clinical Staff**

Welcome to Neurometrix, your AI-powered neuroimaging assistant. This guide will walk you through the clinical workflow using our Streamlit interface.

## 1. Accessing the Platform
Open your web browser (Chrome, Edge, or Firefox) and navigate to the Neurometrix URL provided by your IT department. 

## 2. Uploading a New Study
1. Navigate to the **"Upload Study"** tab.
2. Click on **"Browse files"** and select the 80+ DICOM files for the 4D perfusion study.
3. Verify your **User ID** (e.g., physician@clinic.com).
4. Click the red **"Submit Study"** button.
5. *Wait:* The system will automatically process the mathematical metrics and generate the AI clinical report.

## 3. Reviewing the Clinical Report
1. Click on the **"Case Review"** tab.
2. Select the **Request ID** of the patient you just processed.
3. The screen will display the **Final Report**, including:
   - Quantitative Metrics (CBF, CBV, HU, etc.)
   - Main Findings & Impression
   - Differential Diagnosis
4. Review the AI-generated text carefully. The AI strictly bases its analysis on the deterministic C++ math engine to ensure zero hallucinations.

## 4. Medical Board Decision (Sign-Off)
*Neurometrix requires a certified physician to finalize every report.*
1. Scroll to the bottom of the **"Case Review"** tab.
2. Under **"Medical Board Decision"**, select one of the following:
   - **Approve:** Validates the AI findings.
   - **Reject:** Discards the AI findings.
   - **Request Support:** Flags the case for technical review.
3. You **MUST** write a **Justification** in the text box.
4. Click **"Submit Decision"**.
5. A green success message will appear, confirming the report has been legally signed and saved to the secure PostgreSQL database.
