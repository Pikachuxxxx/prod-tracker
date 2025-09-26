import os
import glob
import datetime
import subprocess

# ----------- CONFIGURATION -----------
DATA_DIR = os.path.expanduser("~/.productivity_tracker")
WEEK_DAYS = 7
OLLAMA_MODEL = "mistral"  # Or your preferred Ollama model name
USE_OLLAMA = True         # Set to False to force GPT fallback
SUMMARY_FILENAME = "ai_weekly_summary.txt"

# ----------- HELPERS -----------------
def recent_files_from_patterns(data_dir, patterns, cutoff_days=WEEK_DAYS):
    cutoff = datetime.datetime.now() - datetime.timedelta(days=cutoff_days)
    matched_files = set()
    for pat in patterns:
        full_pat = os.path.join(data_dir, pat)
        for f in glob.glob(full_pat):
            try:
                mtime = datetime.datetime.fromtimestamp(os.path.getmtime(f))
                if mtime >= cutoff:
                    matched_files.add(f)
            except Exception:
                continue
    return sorted(matched_files)

def load_logs(files):
    contents = []
    for f in files:
        try:
            with open(f, "r", encoding="utf-8") as infile:
                contents.append(f"--- {os.path.basename(f)} ---\n" + infile.read())
        except Exception as e:
            print(f"Error reading {f}: {e}")
    return "\n\n".join(contents)

def build_prompt(logs):
    return (
        "Here are my productivity logs for this week. Please analyze and suggest ways I can "
        "improve my workflow, manage energy, and reduce distractions. "
        "Summarize patterns, recommend changes, and highlight both strengths and weaknesses.\n\n"
        "LOGS:\n" + logs
    )

def query_ollama(prompt, model=OLLAMA_MODEL):
    try:
        result = subprocess.run(
            ["ollama", "run", model],
            input=prompt.encode("utf-8"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=120
        )
        if result.returncode == 0:
            return result.stdout.decode("utf-8").strip()
        else:
            print("Ollama failed: " + result.stderr.decode("utf-8"))
    except Exception as e:
        print(f"Ollama error: {e}")
    return None

def query_gpt_fallback(prompt):
    print("\n--- PROMPT FOR CHATGPT ---\n")
    print(prompt)
    print("\nPaste the above prompt into ChatGPT or Copilot Chat for analysis.\n")
    return None

def write_summary(summary, data_dir, fname=SUMMARY_FILENAME):
    path = os.path.join(data_dir, fname)
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(summary + "\n")
        print(f"\nSummary written to: {path}")
    except Exception as e:
        print(f"Failed to write summary file: {e}")

# ----------- MAIN SCRIPT -------------
def main():
    patterns = [
        "weekly_status_export_*",
        "tasks*",
        "hourly_logs_today_*",
        "dailt_statu_*",
        "daily_status_*",
        "daily_logs.txt"
    ]
    week_files = recent_files_from_patterns(DATA_DIR, patterns, cutoff_days=WEEK_DAYS)
    if not week_files:
        print("No productivity logs found for the last week in:", DATA_DIR)
        return

    logs = load_logs(week_files)
    prompt = build_prompt(logs)

    print("Found logs for analysis from these files:")
    for f in week_files:
        print("  ", f)

    print("\nAnalyzing logs...\n")
    summary = None

    if USE_OLLAMA:
        print(f"Trying Ollama with model '{OLLAMA_MODEL}' ...")
        summary = query_ollama(prompt)

    if not summary:
        print("Falling back to manual ChatGPT prompt.")
        query_gpt_fallback(prompt)
    else:
        print("\n--- AI Productivity Summary ---\n")
        print(summary)
        write_summary(summary, DATA_DIR)

if __name__ == "__main__":
    main()