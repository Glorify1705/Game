CREATE TABLE IF NOT EXISTS images(id INTEGER PRIMARY KEY AUTOINCREMENT,
                                  name VARCHAR(255) UNIQUE NOT NULL,
                                  width INT NOT NULL, height INT NOT NULL,
                                  components INT NOT NULL,
                                  contents BLOB NOT NULL);

CREATE UNIQUE INDEX IF NOT EXISTS idx_images_name ON images(name);

CREATE TABLE IF NOT EXISTS spritesheets(
    id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(255) UNIQUE NOT NULL,
    image VARCHAR(255) NOT NULL, sprites INT NOT NULL,
    sprite_name_length INT NULL, width INT NOT NULL, height INT NOT NULL);

CREATE UNIQUE INDEX IF NOT EXISTS idx_spritesheets_name ON spritesheets(name);

CREATE TABLE IF NOT EXISTS sprites(id INTEGER PRIMARY KEY AUTOINCREMENT,
                                   name VARCHAR(255) UNIQUE NOT NULL,
                                   spritesheet VARCHAR(255) NOT NULL,
                                   x INT NOT NULL, y INT NOT NULL,
                                   width INT NOT NULL, height INT NOT NULL);

CREATE UNIQUE INDEX IF NOT EXISTS idx_sprites_name ON sprites(name);
CREATE INDEX IF NOT EXISTS idx_sprites_spritesheet ON sprites(spritesheet);

CREATE TABLE IF NOT EXISTS audios(id INTEGER PRIMARY KEY AUTOINCREMENT,
                                  name VARCHAR(255) UNIQUE NOT NULL,
                                  contents BLOB NOT NULL);

CREATE TABLE IF NOT EXISTS scripts(id INTEGER PRIMARY KEY AUTOINCREMENT,
                                   name VARCHAR(255) UNIQUE NOT NULL,
                                   contents BLOB NOT NULL);

CREATE TABLE IF NOT EXISTS shaders(id INTEGER PRIMARY KEY AUTOINCREMENT,
                                   name VARCHAR(255) UNIQUE NOT NULL,
                                   shader_type VARCHAR(255) NOT NULL,
                                   contents BLOB NOT NULL);

CREATE TABLE IF NOT EXISTS fonts(id INTEGER PRIMARY KEY AUTOINCREMENT,
                                 name VARCHAR(255) UNIQUE NOT NULL,
                                 contents BLOB NOT NULL);

CREATE TABLE IF NOT EXISTS text_files(id INTEGER PRIMARY KEY AUTOINCREMENT,
                                      name VARCHAR(255) UNIQUE NOT NULL,
                                      contents BLOB NOT NULL);

CREATE TABLE IF NOT EXISTS asset_metadata(id INTEGER PRIMARY KEY AUTOINCREMENT,
                                          name VARCHAR(255) UNIQUE NOT NULL,
                                          type VARCHAR(255) NOT NULL,
                                          size INTEGER NOT NULL,
                                          hash_low INTEGER NOT NULL,
                                          hash_high INTEGER NOT NULL);

CREATE TABLE IF NOT EXISTS compilation_cache(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    source_name VARCHAR(255) UNIQUE NOT NULL, source_hash_low INTEGER NOT NULL,
    source_hash_high INTEGER NOT NULL, compiled BLOB NOT NULL);

CREATE INDEX IF NOT EXISTS idx_compilation_cache ON
    compilation_cache(source_name);

CREATE INDEX IF NOT EXISTS idx_asset_metadata ON asset_metadata(name);

CREATE TABLE IF NOT EXISTS trace_span(id INTEGER PRIMARY KEY AUTOINCREMENT,
                                      parent INTEGER,
                                      name VARCHAR(255) NOT NULL,
                                      begin_source_line VARCHAR(255) NOT NULL,
                                      start TIMESTAMP, end TIMESTAMP);

CREATE TABLE IF NOT EXISTS
trace_span_attribute(id INTEGER PRIMARY KEY AUTOINCREMENT, parent INTEGER,
                     key VARCHAR(255), value VARCHAR(255));
