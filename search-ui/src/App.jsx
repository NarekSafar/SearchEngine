import { useState } from "react";
import "./App.css";

function App() {
  const [query, setQuery] = useState("");
  const [results, setResults] = useState([]);
  const [loading, setLoading] = useState(false);
  const [page, setPage] = useState(1);
  const [limit] = useState(10);
  const [total, setTotal] = useState(0);
  const [mode, setMode] = useState("any");

  const totalPages = Math.ceil(total / limit);

  const fetchResults = async (q, p = 1, m = mode) => {
    setLoading(true);
    try {
      const res = await fetch(
        `http://localhost:8080/search?q=${encodeURIComponent(q)}&page=${p}&limit=${limit}&mode=${m}`
      );
      const data = await res.json();
      setResults(data.results || []);
      setTotal(data.total || 0);
      setPage(data.page || 1);
    } catch (err) {
      console.error(err);
    }
    setLoading(false);
  };

  const handleSearch = (e) => {
    e.preventDefault();
    if (!query.trim()) return;
    fetchResults(query, 1, mode);
  };

  const goToPage = (p) => {
    if (p < 1 || p > totalPages) return;
    fetchResults(query, p, mode);
  };

  const changeMode = (m) => {
    setMode(m);
    if (query.trim()) {
      fetchResults(query, 1, m);
    }
  };

  return (
    <div className="container">
      <h1 className="title">Search Engine</h1>

      <form className="searchBox" onSubmit={handleSearch}>
        <input
          type="text"
          placeholder="Type your query..."
          value={query}
          onChange={(e) => setQuery(e.target.value)}
        />
        <button type="submit">Search</button>
      </form>

      <div className="modeToggle">
        <button
          className={mode === "any" ? "active" : ""}
          onClick={() => changeMode("any")}
        >
          All Results
        </button>
        <button
          className={mode === "all" ? "active" : ""}
          onClick={() => changeMode("all")}
        >
          Exact Match
        </button>
      </div>

      {loading && <p className="loading">Loading...</p>}

      <div className="results">
        {results.map((r) => (
          <div key={r.id} className="resultItem">
            <a href={r.url} target="_blank" rel="noreferrer">
              {r.url}
            </a>

            {mode === "any" && r.missing.length > 0 && (
              <div className="missingBlock">
                <span className="missingLabel">Not found in this page:</span>
                <div className="missing">
                  {r.missing.map((w) => (
                    <span key={w} className="missingWord">{w}</span>
                  ))}
                </div>
              </div>
            )}

	  <p className="score">Score: {r.score}</p>
          </div>
        ))}
      </div>

      {totalPages > 1 && (
        <div className="pagination">
          <button onClick={() => goToPage(page - 1)} disabled={page === 1}>
            Prev
          </button>

          {Array.from({ length: totalPages }, (_, i) => i + 1)
            .slice(Math.max(0, page - 3), Math.min(totalPages, page + 2))
            .map((p) => (
              <button
                key={p}
                className={p === page ? "active" : ""}
                onClick={() => goToPage(p)}
              >
                {p}
              </button>
            ))}

          <button onClick={() => goToPage(page + 1)} disabled={page === totalPages}>
            Next
          </button>
        </div>
      )}
    </div>
  );
}

export default App;
