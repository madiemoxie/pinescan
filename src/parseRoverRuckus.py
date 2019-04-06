#!/usr/bin/python3
import json
import sys
import os
import io
import csv
import time
import shutil
import tkinter
from tkinter import messagebox

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

if len(sys.argv) != 3:
    print("Usage: <matchdata.csv> <teamnumberlist.txt>")
    sys.exit(1)

matchdatapath = sys.argv[1]
matchdatapathtemp = matchdatapath + ".temp"
teamnumberlistpath = sys.argv[2]

if not os.path.exists(teamnumberlistpath):
    print("Error: " + matchdatapath + " not found")
    sys.exit(1)

with open(teamnumberlistpath, "r") as f:
    teamnumbers = f.read().splitlines()

print("Loaded " + str(len(teamnumbers)) + " teamnumbers.")

with io.open(matchdatapath, "r", encoding='utf-8-sig', newline='') as csvfile:
    matchdata = list(csv.DictReader(csvfile))

def writeMatchDataFile(path, data):
    with io.open(path, "w", encoding='utf-8-sig', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=data[0].keys()) 
        writer.writeheader()
        writer.writerows(data)

def isTeamNumberValid(teamNumber):
    return str(teamNumber) in teamnumbers

def isMatchNumberValid(matchNumber):
    return matchNumber < 200 or matchNumber == 999

def indexOfAlreadyScannedMatch(match):
    for i in range(len(matchdata)):
        line = matchdata[i]
        if str(line["competition"]) == str(match["competition"]) and str(line["match"]) == str(match["match"]) and str(line["team"]) == str(match["team"]):
            return i
    return -1 


root = tkinter.Tk()
root.withdraw()
root.attributes("-topmost", True)

for line in sys.stdin:
    data = json.loads(line)
    print("Read match")

    results = {
        "competition" : data["qr_data"],
        "match" : parseNumberOrDefault(data, ["match1", "match2", "match3"], ""),
        "team" : parseNumberOrDefault(data, ["team1", "team2", "team3", "team4", "team5"], ""),
        "matchtype" : parseOptionOrDefault(data, "matchtype", 'Qual'),
        "color" : parseOptionOrDefault(data, "color", ''),
        "startingside" : parseOptionOrDefault(data, "side", ''),
        "landed" : parseOptionOrDefault(data, "landed", 0),
        "sampled" : parseOptionOrDefault(data, "sampled", 0),
        "claimed" : parseOptionOrDefault(data, "claimed", 0),
        "parked" : parseOptionOrDefault(data, "parked", 0),
        "lander" : parseNumberOrDefault(data, ["lander1", "lander2"], 0),
        "depot" : parseNumberOrDefault(data, ["depot1", "depot2"], 0),
        "hanging" : parseOptionOrDefault(data, "hanging", 0),
        "partincrater" : parseOptionOrDefault(data, "partincrater", 0),
        "fullyincrater" : parseOptionOrDefault(data, "fullyincrater", 0),
        "defense" : parseOptionOrDefault(data, "defense", 0),
        "minor" : parseOptionOrDefault(data, "minor", 0),
        "major" : parseOptionOrDefault(data, "major", 0),
        "goldhold" : parseOptionOrDefault(data, "goldhold", 0),
        "silverhold" : parseOptionOrDefault(data, "silverhold", 0),
        "disconnect" : parseOptionOrDefault(data, "disconnect", 0)
    }

    print(results["team"])

    teamNumberError = ""
    if not isTeamNumberValid(results["team"]):
        teamNumberError = "Team number is invalid!\n"
        print("Skipped match due to invalid team number.")

    matchNumberError = ""
    if not isMatchNumberValid(results["match"]):
        matchNumberError = "Match number is invalid!\n"
        print("Skipped match due to invalid match number.")

    if indexOfAlreadyScannedMatch(results) != -1: 
        duplicateMatchError = "Match has already been scanned!\n"
        doOverwrite = messagebox.askyesno("Error", "This match has already been scanned;\n would you like to overwrite the previous scan?")
        if not doOverwrite:
            print("Skipped match due to being a duplicate.")
            continue
        else: 
            matchdata[indexOfAlreadyScannedMatch(results)] = results
            writeMatchDataFile(matchdatapathtemp, matchdata)
            shutil.move(matchdatapathtemp, matchdatapath)
            print("Overwrote match at index " + str(indexOfAlreadyScannedMatch(results)))
            continue

    errorMessage = teamNumberError + matchNumberError

    if errorMessage != "":
        print(errorMessage)
        root.lift()
        messagebox.showerror("Error", errorMessage)
        root.lift()
        continue

    matchdata.append(results)
    headers = ','.join(map(str, results.keys()))
    data = ','.join(map(str, results.values()))
    if len(sys.argv) > 2:
        filename = matchdatapath
        exists = os.path.exists(filename)
        with open(filename, "a+") as f:
            if not exists:
                f.write(headers + "\n")
            f.write(data + "\n")
            print(data)
            