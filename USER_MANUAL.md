# User & Deployment Manual

## Prerequisites
- Windows 10/11 or Linux
- Python 3.13+
- C++20 Compiler
- Apache Kafka on port 9092
- PostgreSQL on port 5432

## Steps to Run
1. Setup database 
eurometrix_core_4d.
2. Copy .env.example to .env.
3. Start C++ Core: cd neuro-4d/build and run ./bin/Release/neuro-4d.exe.
4. Start Agents: cd agents and run python supervisor.py.
5. Start Frontend: cd agents/frontend and run streamlit run app.py.
