#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Uso: $0 <EC2_PUBLIC_IP_OR_DOMAIN>"
  exit 1
fi

BACKEND_HOST=$1

cat > .env.production <<EOV
VITE_API_BASE_URL=http://${BACKEND_HOST}:8080
EOV

echo ".env.production generado con VITE_API_BASE_URL=http://${BACKEND_HOST}:8080"
echo "Ahora ejecuta: npm install && npm run build"
