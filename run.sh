#!/usr/bin/bash
#!/usr/bin/env bash

set -euo pipefail

# Repository root
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Library path for sophon-stream
export LD_LIBRARY_PATH="${ROOT_DIR}/build/lib:${LD_LIBRARY_PATH:-}"

# Runner directory
cd "${ROOT_DIR}/samples/build"

if [[ ! -f ./main ]]; then
	echo "ERROR: ./main not found in samples/build" >&2
	exit 1
fi
chmod +x ./main || true

mkdir -p "${ROOT_DIR}/logs"

echo "Launching 8 yolov5 single-model processes (config_1..8) ..."

launched=0
for i in $(seq 1 8); do
	cfg="../yolov5/config_${i}/yolov5_demo.json"
	log="${ROOT_DIR}/logs/config_${i}.log"
	if [[ -f "${cfg}" ]]; then
		echo " -> ${cfg} | log: ${log}"
		./main --demo_config_path="${cfg}" >"${log}" 2>&1 &
		launched=$((launched+1))
	else
		echo "WARN: ${cfg} not found, skipping" >&2
	fi
done

if [[ ${launched} -eq 0 ]]; then
	echo "ERROR: No configs found under ../yolov5/config_{1..8}/yolov5_demo.json" >&2
	exit 1
fi

wait
echo "All ${launched} process(es) finished. Logs in ${ROOT_DIR}/logs."
fi


