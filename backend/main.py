from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
import os

app = FastAPI(title="Remote Driving Backend")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 读取环境变量示例
DATABASE_URL = os.getenv("DATABASE_URL", "postgresql://user:pass@localhost/db")
REDIS_URL = os.getenv("REDIS_URL", "redis://localhost")

@app.get("/")
async def root():
    return {
        "message": "Backend Service is running",
        "db": DATABASE_URL,
        "redis": REDIS_URL
    }

@app.get("/health")
async def health():
    return {"status": "ok"}
