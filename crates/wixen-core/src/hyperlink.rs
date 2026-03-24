//! OSC 8 hyperlink storage.
//!
//! Terminals can embed explicit hyperlinks using the OSC 8 escape sequence.
//! Opening: `ESC ] 8 ; params ; uri ST`. Closing: `ESC ] 8 ; ; ST`.
//!
//! Each unique (uri, id) pair gets a numeric ID stored in `CellAttributes::hyperlink_id`.
//! The `HyperlinkStore` owns the mapping from numeric IDs to URIs.

/// A single hyperlink target.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Hyperlink {
    /// The URI this link points to.
    pub uri: String,
    /// Optional explicit ID (for grouping discontinuous runs of the same link).
    pub id: Option<String>,
}

/// Central store for hyperlinks referenced by cells.
///
/// Index 0 is reserved (means "no link"). Real links start at index 1.
#[derive(Debug)]
pub struct HyperlinkStore {
    /// All registered hyperlinks. Index 0 is unused (sentinel).
    links: Vec<Hyperlink>,
    /// The currently-open hyperlink ID (0 = none).
    pub active_id: u32,
}

impl HyperlinkStore {
    pub fn new() -> Self {
        Self {
            // Index 0 = sentinel (no link).
            links: vec![Hyperlink {
                uri: String::new(),
                id: None,
            }],
            active_id: 0,
        }
    }

    /// Register a hyperlink and return its ID. If an identical (uri, id) pair
    /// already exists, returns the existing ID (dedup).
    pub fn get_or_insert(&mut self, uri: &str, id: Option<&str>) -> u32 {
        // Search for an existing match (skip index 0).
        for (i, link) in self.links.iter().enumerate().skip(1) {
            if link.uri == uri && link.id.as_deref() == id {
                return i as u32;
            }
        }

        let idx = self.links.len() as u32;
        self.links.push(Hyperlink {
            uri: uri.to_string(),
            id: id.map(|s| s.to_string()),
        });
        idx
    }

    /// Look up a hyperlink by ID.
    pub fn get(&self, id: u32) -> Option<&Hyperlink> {
        if id == 0 {
            return None;
        }
        self.links.get(id as usize)
    }

    /// Open a hyperlink — subsequent printed cells will carry this ID.
    pub fn open(&mut self, uri: &str, id: Option<&str>) {
        self.active_id = self.get_or_insert(uri, id);
    }

    /// Close the current hyperlink.
    pub fn close(&mut self) {
        self.active_id = 0;
    }
}

impl Default for HyperlinkStore {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_new_store_has_sentinel() {
        let store = HyperlinkStore::new();
        assert_eq!(store.active_id, 0);
        assert!(store.get(0).is_none());
    }

    #[test]
    fn test_get_or_insert_returns_id() {
        let mut store = HyperlinkStore::new();
        let id1 = store.get_or_insert("https://example.com", None);
        assert_eq!(id1, 1);
        let id2 = store.get_or_insert("https://other.com", None);
        assert_eq!(id2, 2);
    }

    #[test]
    fn test_dedup_same_uri() {
        let mut store = HyperlinkStore::new();
        let id1 = store.get_or_insert("https://example.com", None);
        let id2 = store.get_or_insert("https://example.com", None);
        assert_eq!(id1, id2);
    }

    #[test]
    fn test_dedup_with_explicit_id() {
        let mut store = HyperlinkStore::new();
        let id1 = store.get_or_insert("https://example.com", Some("link1"));
        let id2 = store.get_or_insert("https://example.com", Some("link1"));
        let id3 = store.get_or_insert("https://example.com", Some("link2"));
        assert_eq!(id1, id2);
        assert_ne!(id1, id3);
    }

    #[test]
    fn test_get_returns_hyperlink() {
        let mut store = HyperlinkStore::new();
        let id = store.get_or_insert("https://example.com", Some("myid"));
        let link = store.get(id).unwrap();
        assert_eq!(link.uri, "https://example.com");
        assert_eq!(link.id.as_deref(), Some("myid"));
    }

    #[test]
    fn test_open_close() {
        let mut store = HyperlinkStore::new();
        store.open("https://example.com", None);
        assert_ne!(store.active_id, 0);
        let active = store.get(store.active_id).unwrap();
        assert_eq!(active.uri, "https://example.com");

        store.close();
        assert_eq!(store.active_id, 0);
    }

    #[test]
    fn test_open_overrides_previous() {
        let mut store = HyperlinkStore::new();
        store.open("https://first.com", None);
        let first_id = store.active_id;
        store.open("https://second.com", None);
        let second_id = store.active_id;
        assert_ne!(first_id, second_id);
        assert_eq!(
            store.get(store.active_id).unwrap().uri,
            "https://second.com"
        );
    }
}
