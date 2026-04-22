# Manual de Usuario Proyecto 2 - EXTREAMFS

## 1. Requisitos

- Backend ejecutandose en puerto `8080`
- Frontend React disponible (local o S3)

## 2. Inicio Rapido

1. Abrir la interfaz web.
2. Escribir o cargar un script `.smia`.
3. Ejecutar comandos desde el editor.
4. Revisar salida textual y reportes.

## 3. Flujo Recomendado de Demo

1. Crear disco y particion:
```txt
mkdisk -size=20 -unit=M -path=/tmp/demo.mia
fdisk -size=10 -unit=M -path=/tmp/demo.mia -name=Part1
mount -path=/tmp/demo.mia -name=Part1
```

2. Formatear y loguear:
```txt
mkfs -id=231A -type=full -fs=3fs
login -user=root -pass=123 -id=231A
```

3. Crear carpetas/archivos:
```txt
mkdir -p -path=/home/docs
mkfile -path=/home/docs/a.txt -contenido="hola"
cat -file1=/home/docs/a.txt
```

4. Probar comandos P2:
```txt
copy -path=/home/docs/a.txt -destino=/home
rename -path=/home/a.txt -name=b.txt
move -path=/home/b.txt -destino=/home/docs
find -path=/home -name=*.txt
chmod -path=/home/docs/b.txt -ugo=664
chown -path=/home/docs/b.txt -user=root
remove -path=/home/docs/b.txt
```

5. Journaling / Loss / Recovery:
```txt
journaling -id=231A
loss -id=231A
recovery -id=231A
```

6. Salida:
```txt
logout
unmount -id=231A
mounted
```

## 4. Carga de Scripts

Desde la GUI se puede cargar archivo `.smia` y ejecutarlo completo.
Scripts recomendados del proyecto:

- `demo_p2.smia`
- `prueba_auxiliar_compat_p2.smia`

## 5. Visualizacion de Reportes

Al generar reportes con `rep`, pueden verse desde:

- panel de reportes de la GUI
- endpoint `GET /report?path=...`

## 6. Errores Comunes

- `id no encontrado`: particion no montada o ID incorrecto.
- `permiso denegado`: usuario sin permisos.
- `ruta no encontrada`: path invalido.
- `carpeta no vacia (usa -r)`: usar `remove -r` para recursivo.

## 7. Nota de IDs por Carnet

El proyecto usa prefijo `23` (ultimos dos del carnet `202202123`) en IDs montadas.
Ejemplo: `231A`, `232A`, `231B`.

