#!/usr/bin/env python3
"""Stateful process boundary fixture; never connects to a Docker daemon."""
import json
import os
import re
import signal
import sys
from pathlib import Path


def main():
    state_path = Path(os.environ["DEPLOY_FIXTURE_STATE"])
    state = json.loads(state_path.read_text())
    executable = Path(sys.argv[0]).name
    args = sys.argv[1:]
    state["calls"].append({
        "command": executable,
        "args": ["MYSQL_PWD=<redacted>" if arg.startswith("MYSQL_PWD=") else arg for arg in args],
        "image": os.environ.get("HTTPSERVER_IMAGE", ""),
        "images": state["images"].copy(),
    })

    def finish(code=0, output=""):
        state_path.write_text(json.dumps(state))
        if output:
            print(output)
        raise SystemExit(code)

    def fail_once(name):
        if state["scenario"] == name and name not in state["failed"]:
            state["failed"].append(name)
            return True
        return False

    def mysql(command):
        if "-e" in command:
            sql = command[command.index("-e") + 1]
            if sql.startswith("SELECT COUNT(*)"):
                version = re.search(r"version = '([^']+)'", sql).group(1)
                finish(output=str(int(version in state["versions"])))
            if sql.startswith("INSERT IGNORE"):
                version = re.search(r"VALUES \('([^']+)'\)", sql).group(1)
                if version not in state["versions"]:
                    state["versions"].append(version)
            elif not sql.startswith("CREATE TABLE IF NOT EXISTS schema_migrations"):
                raise AssertionError(f"unexpected SQL: {sql}")
            finish()
        sql = sys.stdin.read()
        version = re.search(r"VALUES \('([^']+)'\)", sql).group(1)
        if state["scenario"] == "migration":
            finish(41)
        if version in state["versions"]:
            finish(42, "duplicate migration execution")
        state["migrations"].append(version)
        state["versions"].append(version)
        finish()

    if executable == "sleep":
        finish()
    if executable == "curl":
        candidate = any(not value.startswith("sha256:") for value in state["images"].values())
        finish(22 if state["scenario"] == "smoke" and candidate else 0)
    if executable == "mysql":
        mysql(args)
    if args[0] == "inspect":
        service = args[-1]
        if ".Health.Status" in args[-2]:
            finish(output="unhealthy" if state["scenario"] == "dependency" else "healthy")
        finish(output=state["images"][service])
    if args[:2] != ["compose", "-f"]:
        raise AssertionError(f"unexpected command: {args}")
    command = args[3:]
    if command[0] == "ps":
        service = command[-1]
        finish(output=service if service in state["images"] or service in ("mysql", "redis") else "")
    if command[0] == "pull":
        finish(31 if state["scenario"] == "pull" else 0)
    if command[0] == "up":
        services = [service for service in command if service in ("webserver-a", "webserver-b")]
        image = os.environ["HTTPSERVER_IMAGE"]
        if services:
            if state["scenario"] == "rollback" and image.startswith("sha256:"):
                finish(43)
            for service in services:
                state["images"][service] = image
            if fail_once("activation"):
                finish(32)
        finish()
    if command[0] == "stop":
        state["stopped"].append(command[-1])
        finish()
    if command[:2] == ["exec", "-T"]:
        rest = command[2:]
        if rest[:1] == ["-e"]:
            rest = rest[2:]
        service, program, *program_args = rest
        if program == "mysqldump":
            finish(33 if state["scenario"] == "backup" else 0, "-- fixture SQL dump")
        if program == "mysql":
            mysql(program_args)
        if program == "curl":
            if fail_once("termination"):
                state_path.write_text(json.dumps(state))
                os.kill(os.getppid(), signal.SIGTERM)
            candidate = not state["images"][service].startswith("sha256:")
            failing = state["scenario"] in (service, "rollback") and candidate
            finish(22 if failing else 0)
        if program == "nginx":
            stage = "nginx_config" if program_args == ["-t"] else "nginx_reload"
            finish(34 if fail_once(stage) else 0)
    raise AssertionError(f"unexpected command: {args}")


if __name__ == "__main__":
    main()
