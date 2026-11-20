#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Loopback-only optional OTA statistics; reports are read from the server shell."""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import re
import sqlite3
import time


SERIAL = re.compile(r'[a-f0-9]{32}')
VERSION = re.compile(r'(?:0|[1-9][0-9]{0,4})(?:\.(?:0|[1-9][0-9]{0,4})){2}(?:-beta\.[1-9][0-9]{0,4})?')
WINDOW_DAYS = 90


def current_day():
    return int(time.time()) // 86400


@contextmanager
def database(path):
    db = sqlite3.connect(path, timeout=0.5)
    try:
        db.execute('PRAGMA secure_delete=ON')
        with db:
            db.execute('''CREATE TABLE IF NOT EXISTS installs (
                serial TEXT PRIMARY KEY, firmware TEXT NOT NULL, last_day INTEGER NOT NULL
            ) WITHOUT ROWID''')
            yield db
    finally:
        db.close()


def purge(db, day):
    db.execute('DELETE FROM installs WHERE last_day <= ?', (day - WINDOW_DAYS,))


def apply_request(db, path, serial, version, consent):
    if not SERIAL.fullmatch(serial) or serial == '0' * 32:
        return 400
    if path == '/forget':
        db.execute('DELETE FROM installs WHERE serial = ?', (serial,))
        return 204
    if path != '/record' or consent != '1' or not VERSION.fullmatch(version):
        return 400
    db.execute('''INSERT INTO installs(serial, firmware, last_day) VALUES (?, ?, ?)
        ON CONFLICT(serial) DO UPDATE SET firmware = excluded.firmware, last_day = excluded.last_day''',
               (serial, version, current_day()))
    return 204


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        self.request.settimeout(2)
        super().setup()

    def log_message(self, *args):
        pass

    def do_POST(self):
        status = 404
        if self.path in ('/record', '/forget'):
            try:
                with database(self.server.db_path) as db:
                    status = apply_request(db, self.path, self.headers.get('X-Ifink-Serial', ''),
                                           self.headers.get('X-Ifink-Version', ''),
                                           self.headers.get('X-Ifink-Consent', ''))
            except sqlite3.Error:
                status = 503
        self.send_response(status)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', '0')
        self.end_headers()


def serve(path, port):
    with HTTPServer(('127.0.0.1', port), Handler) as http:
        http.db_path = path
        http.timeout = 5
        next_purge = 0
        while True:
            now = time.monotonic()
            if now >= next_purge:
                try:
                    with database(path) as db:
                        purge(db, current_day())
                except sqlite3.Error:
                    print('Could not prune expired OTA statistics', flush=True)
                next_purge = now + 3600
            http.handle_request()


def report(path):
    with database(path) as db:
        day = current_day()
        purge(db, day)
        rows = db.execute('''SELECT firmware, count(*) FROM installs
            WHERE last_day > ? GROUP BY firmware ORDER BY count(*) DESC, firmware''',
                          (day - WINDOW_DAYS,)).fetchall()
    date = datetime.now(timezone.utc).date().isoformat()
    print(f'{date} UTC | last {WINDOW_DAYS} days | participating installations: {sum(n for _, n in rows)}')
    for version, count in rows:
        print(f'{version}\t{count}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('serve', 'report'))
    parser.add_argument('--db', type=Path, required=True)
    parser.add_argument('--port', type=int, default=8769)
    args = parser.parse_args()
    if args.command == 'serve':
        serve(args.db, args.port)
    else:
        report(args.db)


if __name__ == '__main__':
    main()
