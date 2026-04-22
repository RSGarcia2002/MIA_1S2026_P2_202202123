# Arquitectura Proyecto 2

## Topologia de Despliegue

Frontend (React/Vite) en S3  
-> HTTP/JSON  
-> Backend C++ en EC2 (`:8080`)  
-> Disco virtual `.mia` en filesystem del host

## Componentes Backend

- `Analyzer`
  - Parsea linea por linea
  - Valida parametros
  - Despacha comandos
- `DiskManagement`
  - Discos y particiones
  - Montaje / desmontaje
  - IDs con prefijo de carnet (`23`)
- `FileSystem`
  - `mkfs` EXT2/EXT3
  - journaling
  - `loss` y `recovery`
- `FileOperations`
  - Directorios/archivos
  - permisos/propietario
  - operaciones P2 (`copy`, `move`, `find`, etc.)
- `UserSession`
  - login/logout
  - grupos y usuarios
- `Reports`
  - Reportes dot/png/txt/pdf
- `HttpServer`
  - API REST con sockets POSIX
  - CORS para frontend

## Endpoints

- `POST /execute`
- `GET /health`
- `GET /report`
- `GET /fs/mounted`
- `GET /fs/browse`
- `GET /fs/file`

## Seguridad y Operacion

- CORS habilitado para consumo desde frontend
- SG en AWS:
  - `22` restringido por IP
  - `8080` para consumo de frontend
- Backend como servicio (`systemd`) en EC2

