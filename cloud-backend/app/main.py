import os

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.database import Base, engine
from app.routes import router

app = FastAPI(title="Building monitor API", version="1.0.0")


@app.on_event("startup")
def create_db_tables():
    # register table classes before create_all
    import app.models  # noqa: F401

    Base.metadata.create_all(bind=engine)


# cors so dashboard on localhost can hit this
_default_origins = "http://localhost:5173,http://localhost:3000,http://127.0.0.1:5173"
_origins = [o.strip() for o in os.getenv("CORS_ORIGINS", _default_origins).split(",") if o.strip()]

app.add_middleware(
    CORSMiddleware,
    allow_origins=_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)
app.include_router(router)
