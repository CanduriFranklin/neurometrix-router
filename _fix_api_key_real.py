import os

# 1. Corregir el FRONTEND (app.py)
app_path = r"C:\Users\candu\source\repos\Mathematical_Neurometrix\agents\frontend\app.py"
with open(app_path, "r", encoding="utf-8") as f:
    app_code = f.read()

app_code = app_code.replace(
    'client = OpenAI(base_url="https://developer.amd.com.cn/radeon/api/v1", api_key="dummy-key-for-demo")',
    'client = OpenAI(base_url=os.getenv("AMD_API_BASE", "https://developer.amd.com.cn/radeon/api/v1"), api_key=os.getenv("AMD_API_KEY", "dummy"))'
)

with open(app_path, "w", encoding="utf-8") as f:
    f.write(app_code)

# 2. Corregir el AGENTE INTERFACE (agent.py)
agent_path = r"C:\Users\candu\source\repos\Mathematical_Neurometrix\agents\interface\agent.py"
with open(agent_path, "r", encoding="utf-8") as f:
    agent_code = f.read()

agent_code = agent_code.replace(
    'AMD_API_KEY = "dummy-key-for-hackathon"',
    'AMD_API_KEY = os.getenv("AMD_API_KEY", "dummy")'
)

with open(agent_path, "w", encoding="utf-8") as f:
    f.write(agent_code)

print("✅ API Key conectada al .env en el Frontend y el Agente Interface.")
