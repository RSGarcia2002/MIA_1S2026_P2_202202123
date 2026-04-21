import { useState, useRef, useEffect, useCallback, useMemo } from "react";

const API_BASE = (import.meta.env.VITE_API_BASE_URL || "").replace(/\/$/, "");
const apiUrl = (path) => `${API_BASE}${path}`;

// Extrae TODOS los reportes detectados en un bloque de texto de salida.
// Cada match devuelve { path, name, isImage, id }.
function detectAllReports(outputText) {
  const regex = /generado:\s*(\S+\.(jpg|jpeg|png|svg|pdf|txt))/gi;
  const results = [];
  let match;

  while ((match = regex.exec(outputText)) !== null) {
    const path = match[1];
    const name = path.split("/").pop();
    const isImage = /\.(jpg|jpeg|png|svg|pdf)$/i.test(path);

    results.push({
      path,
      name,
      isImage,
      id: `${path}_${Date.now()}_${Math.random()}`,
    });
  }

  return results;
}

// Extrae el numero de lineas no vacias de los comandos de entrada
function countCommands(text) {
  return text.split("\n").filter((l) => l.trim() && !l.trim().startsWith("#"))
    .length;
}

// Cuenta lineas totales
function countLines(text) {
  if (!text.trim()) return 0;
  return text.split("\n").length;
}

// Muestra el contenido de un archivo .txt de reporte
function ReportText({ url }) {
  const [text, setText] = useState("Cargando...");

  useEffect(() => {
    let cancelled = false;

    fetch(url)
      .then((r) => r.text())
      .then((data) => {
        if (!cancelled) setText(data);
      })
      .catch((e) => {
        if (!cancelled) setText("Error al cargar: " + e.message);
      });

    return () => {
      cancelled = true;
    };
  }, [url]);

  return <pre className="report-text">{text}</pre>;
}

// Tarjeta de reporte en el panel lateral
function ReportCard({ rep, active, onClick }) {
  const typeTag = rep.isImage ? "IMG" : "TXT";
  const typeClass = rep.isImage ? "tag-img" : "tag-txt";

  return (
    <button
      className={`report-card${active ? " active" : ""}`}
      onClick={onClick}
      title={rep.path}
      type="button"
    >
      <span className={`report-tag ${typeClass}`}>{typeTag}</span>
      <span className="report-card-name">{rep.name}</span>
    </button>
  );
}

function QuickAction({ title, onClick }) {
  return (
    <button className="quick-action" type="button" onClick={onClick}>
      {title}
    </button>
  );
}

export default function App() {
  const [input, setInput] = useState("");
  const [output, setOutput] = useState("");
  const [loading, setLoading] = useState(false);
  const [reports, setReports] = useState([]);
  const [activeIdx, setActiveIdx] = useState(null);
  const [backendStatus, setBackendStatus] = useState("idle");
  const [mountedItems, setMountedItems] = useState([]);
  const [selectedId, setSelectedId] = useState("");
  const [browsePath, setBrowsePath] = useState("/");
  const [browseItems, setBrowseItems] = useState([]);
  const [fileView, setFileView] = useState({ path: "", content: "" });
  const [fsLoading, setFsLoading] = useState(false);
  const [fsError, setFsError] = useState("");

  const fileInputRef = useRef(null);
  const outputRef = useRef(null);
  const inputRef = useRef(null);

  // Auto-scroll del textarea de salida
  useEffect(() => {
    if (outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [output]);

  // Selecciona el ultimo reporte al agregar nuevos
  const addReports = useCallback((newReps) => {
    if (newReps.length === 0) return;

    setReports((prev) => {
      const existingPaths = new Set(prev.map((r) => r.path));
      const filtered = newReps.filter((r) => !existingPaths.has(r.path));
      const updated = [...prev, ...filtered];

      if (updated.length > 0) {
        setActiveIdx(updated.length - 1);
      }

      return updated;
    });
  }, []);

  const fetchMounted = useCallback(async () => {
    try {
      const res = await fetch(apiUrl("/fs/mounted"));
      const data = await res.json();
      const items = data.items || [];
      setMountedItems(items);
      if (items.length > 0) {
        setSelectedId((prev) =>
          prev && items.some((i) => i.id === prev) ? prev : items[0].id,
        );
      } else {
        setSelectedId("");
        setBrowseItems([]);
      }
    } catch (e) {
      setFsError("No se pudo cargar particiones montadas: " + e.message);
    }
  }, []);

  const sendCommands = useCallback(
    async (commands) => {
      setBackendStatus("connecting");
      try {
        const res = await fetch(apiUrl("/execute"), {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ commands }),
        });
        const data = await res.json();
        const out = data.output || "";
        setOutput(out);
        addReports(detectAllReports(out));
        setBackendStatus("online");
        await fetchMounted();
        return out;
      } catch (e) {
        setOutput(
          "Error de conexion con el backend: " +
            e.message +
            "\n\nVerifica que el servidor este corriendo en el puerto 8080.",
        );
        setBackendStatus("offline");
        throw e;
      }
    },
    [addReports, fetchMounted],
  );

  const browseFs = useCallback(async (id, path) => {
    if (!id) return;
    setFsLoading(true);
    setFsError("");
    try {
      const res = await fetch(
        apiUrl("/fs/browse?id=") +
          encodeURIComponent(id) +
          "&path=" +
          encodeURIComponent(path || "/"),
      );
      const data = await res.json();
      if (!data.ok) {
        setFsError(data.error || "Error al navegar");
        setBrowseItems([]);
      } else {
        setBrowsePath(data.path || "/");
        setBrowseItems(data.items || []);
      }
    } catch (e) {
      setFsError("Error al consultar FS: " + e.message);
      setBrowseItems([]);
    }
    setFsLoading(false);
  }, []);

  const openFileFromFs = useCallback(async (id, path) => {
    if (!id) return;
    try {
      const res = await fetch(
        apiUrl("/fs/file?id=") +
          encodeURIComponent(id) +
          "&path=" +
          encodeURIComponent(path),
      );
      const data = await res.json();
      if (!data.ok) {
        setFsError(data.error || "No se pudo abrir archivo");
        return;
      }
      setFileView({ path: data.path || path, content: data.content || "" });
    } catch (e) {
      setFsError("Error al abrir archivo: " + e.message);
    }
  }, []);

  useEffect(() => {
    fetchMounted();
  }, [fetchMounted]);

  useEffect(() => {
    if (selectedId) {
      browseFs(selectedId, "/");
    }
  }, [selectedId, browseFs]);

  const execute = async () => {
    if (!input.trim()) return;

    setLoading(true);
    await sendCommands(input).catch(() => {});
    setLoading(false);
  };

  const handleKeyDown = (e) => {
    if (e.ctrlKey && e.key === "Enter") {
      execute();
    }
  };

  const loadFile = (e) => {
    const file = e.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (ev) => setInput(ev.target.result);
    reader.readAsText(file);
    e.target.value = "";
  };

  const clearAll = () => {
    setOutput("");
    setReports([]);
    setActiveIdx(null);
  };

  const clearInput = () => {
    setInput("");
    inputRef.current?.focus();
  };

  const appendTemplate = (template) => {
    setInput((prev) => {
      const base = prev.trim();
      return base ? `${base}\n\n${template}` : template;
    });
    inputRef.current?.focus();
  };

  const loadDemoCommands = () => {
    setInput(`# Demo P2 EXT2/EXT3 + comandos nuevos

mkdisk -size=10 -unit=M -path=/home/randall/demo.mia
fdisk -size=5 -unit=M -path=/home/randall/demo.mia -name=Part1
mount -path=/home/randall/demo.mia -name=Part1
mounted
mkfs -id=231A -fs=3fs
login -user=root -pass=123 -id=231A
mkdir -p -path=/home/proyectos/mia
mkfile -path=/home/proyectos/mia/readme.txt -size=64
copy -path=/home/proyectos/mia/readme.txt -destino=/home
rename -path=/home/readme.txt -name=readme2.txt
find -path=/ -name=*readme*
chmod -path=/home/readme2.txt -ugo=664
cat -file1=/home/proyectos/mia/readme.txt
loss -id=231A
journaling -id=231A
rep -id=231A -path=/home/randall/reporte_mbr.jpg -name=mbr
rep -id=231A -path=/home/randall/reporte_tree.png -name=tree`);
    inputRef.current?.focus();
  };

  const activeReport = activeIdx !== null ? reports[activeIdx] : null;

  const activeUrl = activeReport
    ? apiUrl("/report?path=") + encodeURIComponent(activeReport.path)
    : null;

  const commandCount = useMemo(() => countCommands(input), [input]);
  const inputLines = useMemo(() => countLines(input), [input]);
  const outputLines = useMemo(() => countLines(output), [output]);

  const statusLabel =
    backendStatus === "online"
      ? "Backend conectado"
      : backendStatus === "offline"
        ? "Backend desconectado"
        : backendStatus === "connecting"
          ? "Conectando..."
          : "Sin probar conexion";

  return (
    <div className="app-shell">
      <div className="app">
        {/* Cabecera */}
        <header className="header">
          <div className="header-left">
            <div className="brand-mark">EF</div>

            <div className="header-title">
              <h1>EXTREAMFS</h1>
              <span className="header-sub">
                EXT2 Filesystem Simulator • Randall García • 202202123
              </span>
            </div>
          </div>

          <div className="header-right">
            <div className={`status-pill status-${backendStatus}`}>
              <span className="status-dot" />
              {statusLabel}
            </div>

            <div className="header-hint">Ctrl + Enter</div>
          </div>
        </header>

        {/* Barra secundaria */}
        <section className="toolbar">
          <div className="toolbar-group">
            <QuickAction title="Demo rapida" onClick={loadDemoCommands} />
            <QuickAction
              title="Insertar MKDISK"
              onClick={() =>
                appendTemplate(
                  `mkdisk -size=10 -unit=M -path=/home/randall/disco.mia`,
                )}
            />
            <QuickAction
              title="Insertar MOUNT"
              onClick={() =>
                appendTemplate(
                  `mount -path=/home/randall/disco.mia -name=Part1`,
                )}
            />
            <QuickAction
              title="Insertar REP"
              onClick={() =>
                appendTemplate(
                  `rep -id=231A -path=/home/randall/reporte.jpg -name=mbr`,
                )}
            />
            <QuickAction
              title="Insertar LOSS"
              onClick={() => appendTemplate(`loss -id=231A`)}
            />
            <QuickAction
              title="Insertar JOURNALING"
              onClick={() => appendTemplate(`journaling -id=231A`)}
            />
          </div>

          <div className="toolbar-meta">
            <span>{commandCount} comandos</span>
            <span>{inputLines} lineas</span>
          </div>
        </section>

        {/* Paneles principales */}
        <div className="workspace">
          {/* Entrada */}
          <section className="panel panel-editor">
            <div className="panel-header">
              <div className="panel-heading">
                <span className="panel-title">Editor de comandos</span>
                <span className="panel-subtitle">
                  Escribe scripts .smia o comandos individuales
                </span>
              </div>

              <div className="btn-group">
                <button
                  className="btn btn-secondary"
                  type="button"
                  onClick={() => fileInputRef.current?.click()}
                >
                  Cargar archivo
                </button>

                <input
                  ref={fileInputRef}
                  type="file"
                  accept=".smia,.mia,.txt"
                  onChange={loadFile}
                  style={{ display: "none" }}
                />

                <button
                  className="btn btn-primary"
                  type="button"
                  onClick={execute}
                  disabled={loading}
                >
                  {loading ? "Ejecutando..." : "Ejecutar"}
                </button>
              </div>
            </div>

            <div className="editor-wrap">
              <textarea
                ref={inputRef}
                className="code-area input-area"
                value={input}
                onChange={(e) => setInput(e.target.value)}
                onKeyDown={handleKeyDown}
                placeholder={`# Script MIA
mkdisk -size=50 -unit=M -path=/home/randall/disco.mia
fdisk -size=10 -unit=M -path=/home/randall/disco.mia -name=Part1
mount -path=/home/randall/disco.mia -name=Part1
mkfs -id=231A
login -user=root -pass=123 -id=231A
rep -id=231A -path=/tmp/mbr.jpg -name=mbr`}
                spellCheck={false}
              />
            </div>

            <div className="panel-footer">
              <div className="panel-footer-left">
                <span>{commandCount} comandos detectados</span>
              </div>

              <div className="panel-footer-right">
                <button
                  className="btn-link"
                  type="button"
                  onClick={clearInput}
                >
                  Limpiar editor
                </button>
              </div>
            </div>
          </section>

          {/* Salida */}
          <section className="panel panel-output">
            <div className="panel-header">
              <div className="panel-heading">
                <span className="panel-title">Terminal de salida</span>
                <span className="panel-subtitle">
                  Resultado de la ejecucion del backend
                </span>
              </div>

              <div className="btn-group">
                <button
                  className="btn btn-secondary"
                  type="button"
                  onClick={clearAll}
                  title="Limpiar salida y reportes"
                >
                  Limpiar todo
                </button>
              </div>
            </div>

            <div className="editor-wrap">
              <textarea
                ref={outputRef}
                className="code-area output-area"
                value={output}
                readOnly
                placeholder="Los resultados apareceran aqui..."
                spellCheck={false}
              />
            </div>

            <div className="panel-footer">
              <div className="panel-footer-left">
                <span>{output ? `${outputLines} lineas` : "Sin salida"}</span>
              </div>

              <div className="panel-footer-right">
                {reports.length > 0 && (
                  <span className="badge">
                    {reports.length} reporte{reports.length !== 1 ? "s" : ""}
                  </span>
                )}
              </div>
            </div>
          </section>
        </div>

        {/* Panel de reportes */}
        <section className={`reports-shell ${reports.length > 0 ? "show" : ""}`}>
          <div className="reports-topbar">
            <div>
              <h2>Explorador de reportes</h2>
              <p>
                Visualiza imagenes y archivos de texto generados durante la
                ejecucion
              </p>
            </div>

            {reports.length > 0 && (
              <button
                className="btn btn-secondary"
                type="button"
                onClick={() => {
                  setReports([]);
                  setActiveIdx(null);
                }}
              >
                Limpiar reportes
              </button>
            )}
          </div>

          {reports.length > 0 ? (
            <div className="reports-section">
              {/* Lista lateral */}
              <div className="reports-list">
                <div className="reports-list-header">
                  <span>Reportes ({reports.length})</span>
                </div>

                <div className="reports-list-body">
                  {reports.map((rep, idx) => (
                    <ReportCard
                      key={rep.id}
                      rep={rep}
                      active={idx === activeIdx}
                      onClick={() => setActiveIdx(idx)}
                    />
                  ))}
                </div>
              </div>

              {/* Visor */}
              <div className="reports-viewer">
                {activeReport ? (
                  <>
                    <div className="reports-viewer-header">
                      <div className="reports-viewer-meta">
                        <span
                          className="reports-viewer-name"
                          title={activeReport.name}
                        >
                          {activeReport.name}
                        </span>

                        <span
                          className="reports-viewer-path"
                          title={activeReport.path}
                        >
                          {activeReport.path}
                        </span>
                      </div>

                      <div className="btn-group">
                        <a
                          className="btn btn-secondary"
                          href={activeUrl}
                          download={activeReport.name}
                        >
                          Descargar
                        </a>

                        <a
                          className="btn btn-secondary"
                          href={activeUrl}
                          target="_blank"
                          rel="noreferrer"
                        >
                          Abrir
                        </a>
                      </div>
                    </div>

                    {activeReport.isImage ? (
                      <div className="report-image-wrap">
                        <img
                          src={activeUrl + "&t=" + Date.now()}
                          alt={activeReport.name}
                          className="report-image"
                        />
                      </div>
                    ) : (
                      <ReportText url={activeUrl} />
                    )}
                  </>
                ) : (
                  <div className="reports-viewer-empty">
                    Selecciona un reporte para verlo aqui
                  </div>
                )}
              </div>
            </div>
          ) : (
            <div className="reports-empty-state">
              <div className="reports-empty-card">
                <h3>No hay reportes generados</h3>
                <p>
                  Ejecuta comandos <code>rep</code> para que aparezcan aqui tus
                  vistas previas.
                </p>
              </div>
            </div>
          )}
        </section>

        <section className="fs-shell">
          <div className="fs-topbar">
            <div>
              <h2>Visualizador del Sistema de Archivos</h2>
              <p>Selecciona particion montada, navega carpetas y abre archivos</p>
            </div>

            <div className="btn-group">
              <button
                className="btn btn-secondary"
                type="button"
                onClick={fetchMounted}
              >
                Recargar IDs
              </button>
            </div>
          </div>

          <div className="fs-controls">
            <label className="fs-control">
              <span>ID montado</span>
              <select
                value={selectedId}
                onChange={(e) => setSelectedId(e.target.value)}
              >
                {mountedItems.length === 0 && <option value="">(sin montajes)</option>}
                {mountedItems.map((m) => (
                  <option key={m.id} value={m.id}>
                    {m.id} - {m.name}
                  </option>
                ))}
              </select>
            </label>

            <div className="btn-group">
              <button
                className="btn btn-secondary"
                type="button"
                onClick={() => browseFs(selectedId, browsePath)}
                disabled={!selectedId || fsLoading}
              >
                Refrescar ruta
              </button>

              <button
                className="btn btn-secondary"
                type="button"
                onClick={() =>
                  selectedId
                    ? sendCommands(`login -user=root -pass=123 -id=${selectedId}`).then(() =>
                        browseFs(selectedId, browsePath),
                      )
                    : null
                }
                disabled={!selectedId}
              >
                Login root
              </button>

              <button
                className="btn btn-secondary"
                type="button"
                onClick={() => sendCommands("logout")}
              >
                Logout
              </button>
            </div>
          </div>

          <div className="fs-pathbar">
            <code>{browsePath}</code>
            <div className="btn-group">
              <button
                className="btn btn-secondary"
                type="button"
                onClick={() => {
                  const p =
                    browsePath === "/"
                      ? "/"
                      : browsePath.split("/").slice(0, -1).join("/") || "/";
                  browseFs(selectedId, p);
                }}
                disabled={!selectedId || browsePath === "/"}
              >
                Subir
              </button>
              <button
                className="btn btn-secondary"
                type="button"
                onClick={() => browseFs(selectedId, "/")}
                disabled={!selectedId}
              >
                Ir a /
              </button>
            </div>
          </div>

          {fsError && <div className="fs-error">{fsError}</div>}

          <div className="fs-grid">
            <div className="fs-list">
              {fsLoading && <div className="fs-empty">Cargando...</div>}
              {!fsLoading && browseItems.length === 0 && (
                <div className="fs-empty">No hay elementos en esta ruta</div>
              )}
              {browseItems.map((item) => (
                <button
                  key={`${item.path}_${item.inode}`}
                  className={`fs-item fs-${item.kind}`}
                  type="button"
                  onClick={() =>
                    item.kind === "dir"
                      ? browseFs(selectedId, item.path)
                      : openFileFromFs(selectedId, item.path)
                  }
                >
                  <span className="fs-item-name">{item.name}</span>
                  <span className="fs-item-meta">
                    {item.kind} • inode {item.inode}
                  </span>
                </button>
              ))}
            </div>

            <div className="fs-viewer">
              {fileView.path ? (
                <>
                  <div className="fs-viewer-head">{fileView.path}</div>
                  <pre className="fs-viewer-body">{fileView.content}</pre>
                </>
              ) : (
                <div className="fs-empty">Selecciona un archivo para ver su contenido</div>
              )}
            </div>
          </div>
        </section>

        <footer className="footer">
          <div className="footer-block">
            <span className="footer-label">Backend</span>
            <code>./build/MIA_P1 --server 8080</code>
          </div>

          <div className="footer-block">
            <span className="footer-label">Frontend</span>
            <code>npm run dev</code>
          </div>

          <div className="footer-block">
            <span className="footer-label">API</span>
            <code>POST /execute</code>
            <code>GET /report?path=</code>
            <code>GET /fs/mounted</code>
            <code>GET /fs/browse?id=&path=</code>
            <code>GET /fs/file?id=&path=</code>
          </div>
        </footer>
      </div>
    </div>
  );
}
