from collections.abc import Generator
from pathlib import Path

from sqlmodel import Session, SQLModel, create_engine

from app.core.config import get_settings

settings = get_settings()
if settings.database_url.startswith("sqlite:///"):
    sqlite_path = Path(settings.database_url.removeprefix("sqlite:///"))
    sqlite_path.parent.mkdir(parents=True, exist_ok=True)

connect_args = {"check_same_thread": False} if settings.database_url.startswith("sqlite") else {}
engine = create_engine(settings.database_url, connect_args=connect_args, echo=False)


def ensure_agent_multi_tenant_uniqueness(connection) -> None:
    indexes = connection.exec_driver_sql("PRAGMA index_list('agent')").fetchall()
    unique_index_columns: list[list[str]] = []
    for index in indexes:
        index_name = index[1]
        is_unique = bool(index[2])
        if not is_unique:
            continue
        index_columns = [
            row[2] for row in connection.exec_driver_sql(f"PRAGMA index_info('{index_name}')").fetchall()
        ]
        unique_index_columns.append(index_columns)

    if ["owner_user_id", "hostname"] in unique_index_columns:
        return

    connection.exec_driver_sql("PRAGMA foreign_keys=OFF")
    try:
        connection.exec_driver_sql(
            """
            CREATE TABLE agent_new (
                id TEXT PRIMARY KEY NOT NULL,
                hostname TEXT NOT NULL,
                owner_user_id TEXT,
                last_api_key_id TEXT,
                display_name TEXT,
                status TEXT NOT NULL DEFAULT 'offline',
                desired_collection_interval_ms INTEGER NOT NULL DEFAULT 1000,
                desired_enable_gpu INTEGER NOT NULL DEFAULT 1,
                last_seen_at TIMESTAMP,
                latest_payload_json TEXT,
                latest_payload_at TIMESTAMP,
                created_at TIMESTAMP NOT NULL,
                updated_at TIMESTAMP NOT NULL,
                FOREIGN KEY(owner_user_id) REFERENCES user(id),
                FOREIGN KEY(last_api_key_id) REFERENCES apikey(id)
            )
            """
        )
        connection.exec_driver_sql(
            """
            INSERT INTO agent_new (
                id,
                hostname,
                owner_user_id,
                last_api_key_id,
                display_name,
                status,
                desired_collection_interval_ms,
                desired_enable_gpu,
                last_seen_at,
                latest_payload_json,
                latest_payload_at,
                created_at,
                updated_at
            )
            SELECT
                id,
                hostname,
                owner_user_id,
                last_api_key_id,
                display_name,
                status,
                desired_collection_interval_ms,
                desired_enable_gpu,
                last_seen_at,
                latest_payload_json,
                latest_payload_at,
                created_at,
                updated_at
            FROM agent
            """
        )
        connection.exec_driver_sql("DROP TABLE agent")
        connection.exec_driver_sql("ALTER TABLE agent_new RENAME TO agent")
        connection.exec_driver_sql("CREATE INDEX ix_agent_hostname ON agent(hostname)")
        connection.exec_driver_sql("CREATE INDEX ix_agent_owner_user_id ON agent(owner_user_id)")
        connection.exec_driver_sql("CREATE INDEX ix_agent_last_api_key_id ON agent(last_api_key_id)")
        connection.exec_driver_sql("CREATE INDEX ix_agent_status ON agent(status)")
        connection.exec_driver_sql("CREATE UNIQUE INDEX uq_agent_owner_hostname ON agent(owner_user_id, hostname)")
    finally:
        connection.exec_driver_sql("PRAGMA foreign_keys=ON")


def create_db_and_tables() -> None:
    SQLModel.metadata.create_all(engine)
    if not settings.database_url.startswith("sqlite"):
        return

    with engine.begin() as connection:
        columns = {
            row[1] for row in connection.exec_driver_sql("PRAGMA table_info('agent')").fetchall()
        }
        upgrades = {
            "owner_user_id": "ALTER TABLE agent ADD COLUMN owner_user_id TEXT",
            "last_api_key_id": "ALTER TABLE agent ADD COLUMN last_api_key_id TEXT",
            "desired_collection_interval_ms": (
                "ALTER TABLE agent ADD COLUMN desired_collection_interval_ms INTEGER NOT NULL DEFAULT 1000"
            ),
            "desired_enable_gpu": (
                "ALTER TABLE agent ADD COLUMN desired_enable_gpu INTEGER NOT NULL DEFAULT 1"
            ),
        }
        for column_name, statement in upgrades.items():
            if column_name not in columns:
                connection.exec_driver_sql(statement)
        ensure_agent_multi_tenant_uniqueness(connection)


def get_session() -> Generator[Session, None, None]:
    with Session(engine) as session:
        yield session
