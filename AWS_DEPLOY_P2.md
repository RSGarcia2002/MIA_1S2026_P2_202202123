# Despliegue AWS Proyecto 2 (S3 + EC2)

## 1. Backend en EC2 (C++)

1. Crear instancia EC2 Linux (Ubuntu 22.04 recomendado).
2. Abrir Security Group:
- SSH `22` (tu IP)
- Backend `8080` (0.0.0.0/0 o restringido segun evaluacion)
3. Instalar dependencias:

```bash
sudo apt update
sudo apt install -y build-essential cmake graphviz libboost-all-dev git
```

4. Subir proyecto al servidor (git clone o scp) y compilar:

```bash
cmake -S . -B build
cmake --build build -j
```

5. Ejecutar backend:

```bash
./build/MIA_P1 --server 8080
```

6. Verificar salud:

```bash
curl http://<EC2_PUBLIC_IP>:8080/health
```

Debe responder `{"ok":true,"service":"MIA_P2"}`.

## 2. Frontend en S3 (sitio estatico)

1. En local, crear `.env.production` con URL del backend EC2:

```bash
VITE_API_BASE_URL=http://<EC2_PUBLIC_IP>:8080
```

2. Construir frontend:

```bash
npm install
npm run build
```

3. Crear bucket S3 para sitio estatico y habilitar static website hosting.
4. Subir contenido de `dist/` al bucket.
5. Configurar bucket policy/public access segun requerimiento del curso.
6. Verificar que el link de S3 carga y se conecta a EC2.

## 3. Evidencias para rubrica

- Captura de EC2 (instancia Linux + backend C++ ejecutando).
- Captura de Security Groups.
- Captura de bucket S3 y Static Website Hosting.
- Captura del enlace del sitio accesible.
- Captura de la GUI navegando particion/carpeta/archivo y contenido.
- Captura de comandos nuevos + `loss` + `journaling`.

## 4. Troubleshooting rapido

- Si S3 no llama al backend: revisar `VITE_API_BASE_URL` y CORS del backend.
- Si frontend no refleja cambios: reconstruir (`npm run build`) y volver a subir `dist/`.
- Si EC2 no responde: revisar puerto 8080 y servicio backend levantado.

