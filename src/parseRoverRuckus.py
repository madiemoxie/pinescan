#!/usr/bin/python3
import json
import sys
import os

THRESHOLD = 0.3

def parseOption(data, key):
    keys = [(k, v) for (k, v) in data.items() if k.startswith(key + '.')]
    bestResult = max(keys, key=lambda item: item[1])
    if bestResult[1] > THRESHOLD:
        return bestResult[0][len(key) + 1:]

def parseOptionOrDefault(data, key, default):
    result = parseOption(data, key)
    return default if result is None else result

def parseNumberOrDefault(data, digitKeys, default):
    digits = []
    for k in digitKeys:
        option = parseOption(data, k)
        if option is not None:
            digits.append(option)
    return int(''.join(digits)) if len(digits) > 0 else default

def parseVector(data, keys):
    return [data[k] > THRESHOLD for k in keys]

for line in sys.stdin:
    data = json.loads(line)

    results = {
        "match" : parseNumberOrDefault(data, ["match1", "match2", "match3"], ""),
        "team" : parseNumberOrDefault(data, ["team1", "team2", "team3", "team4", "team5"], ""),
        "color" : parseOptionOrDefault(data, "color", ''),
        "landed" : parseOptionOrDefault(data, "landed", 0),
        "sampled" : parseOptionOrDefault(data, "sampled", 0),
        "claimed" : parseOptionOrDefault(data, "claimed", 0),
        "parked" : parseOptionOrDefault(data, "parked", 0),
        "landerMinerals" : parseNumberOrDefault(data, ["lander1", "lander2"], ""),
        "depotMinerals" : parseNumberOrDefault(data, ["depot1", "depot2", "match3"], ""),
        "other" : parseOptionOrDefault(data, "other", 0),
        "hanging" : parseOptionOrDefault(data, "hanging", 0),
        "partincrater" : parseOptionOrDefault(data, "partincrater", 0),
        "fullyincrater" : parseOptionOrDefault(data, "fullyincrater", 0),
    }

    headers = ','.join(map(str, results.keys()))
    data = ','.join(map(str, results.values()))
    print(data)
    if len(sys.argv) > 1:
        filename = sys.argv[1]
        exists = os.path.exists(filename)
        with open(filename, "a+") as f:
            if not exists:
                f.write(headers + "\n")
            f.write(data + "\n")