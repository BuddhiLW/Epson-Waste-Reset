#!/usr/bin/env python3
import tomllib
import json
import urllib.request

URL = "https://codeberg.org/atufi/reinkpy/raw/branch/main/reinkpy/epson.toml"

print(f"[*] Fetching upstream database from {URL}...")

try:
    req = urllib.request.Request(URL, headers={'User-Agent': 'Mozilla/5.0 (EWR-Updater)'})
    response = urllib.request.urlopen(req)
    toml_data = response.read().decode('utf-8')

    parsed = tomllib.loads(toml_data)
    ewr_database = {}

    for printer in parsed.get("EPSON",[]):
        if "models" not in printer or "wkey" not in printer:
            continue
        
        for model in printer["models"]:
            ewr_database[model] = {
                "rkey": printer.get("rkey", 0),
                "wkey": printer.get("wkey", ""),
                "addresses":[],
                "reset":[]
            }
            
            for mem in printer.get("mem",[]):
                desc = mem.get("desc", "")
                if "Waste" in desc or "Platen" in desc:
                    addrs = mem.get("addr", [])
                    ewr_database[model]["addresses"].extend(addrs)
                    
                    if "reset" in mem:
                        ewr_database[model]["reset"].extend(mem["reset"])
                    else:
                        ewr_database[model]["reset"].extend([0] * len(addrs))

    with open("database.json", "w", encoding="utf-8") as f:
        json.dump(ewr_database, f, indent=4)
        
    print(f"[+] Success! Generated database.json with {len(ewr_database)} supported models.")

except Exception as e:
    print(f"[-] FATAL ERROR: Could not update database. {e}")
    exit(1)