# Estado y limitaciones

> 🌐 English: [status.md](../status.md)

Esta página ofrece un recuento honesto de lo que está verificado, lo que tiene limitaciones y lo que está planificado. Nada está oculto.

---

## Versión actual

**v0.1.0** — versión temprana.

Desarrollada y probada en macOS + PHP 8.3 NTS. Linux y ZTS son compatibles por diseño: la caché de vocabulario global del proceso usa un mutex TSRM bajo ZTS, de modo que los hilos paralelos no compiten en la población de la caché. La matriz de CI no incluye macOS × ZTS (setup-php no puede proveer esa combinación); Linux cubre el caso de ZTS.

---

## Qué está verificado

- `make install` produce una extensión cargable: `php -m` lista `tokenizers`, `\Tokenizers\VERSION` devuelve `"0.1.0"`.
- La suite de pruebas completa pasa contra la copia instalada.
- Conformidad exacta a nivel de byte para los tres algoritmos de tokenización (ver tabla a continuación).

---

## Conformidad

| Algoritmo | Referencia | Casos de prueba | Resultado |
|---|---|---|---|
| BPE (`cl100k_base` + `o200k_base`) | Python `tiktoken` | 18 | Exactitud a nivel de byte. CI falla ante cualquier diferencia. |
| WordPiece (`bert-base-uncased`) | HuggingFace `BertTokenizerFast` | 44 | Exactitud a nivel de byte. CI falla ante cualquier diferencia. |
| Unigram (`t5-small`) | HuggingFace `transformers` | 24 | Exactitud a nivel de byte. CI falla ante cualquier diferencia. |

Los fixtures de conformidad están confirmados en el repositorio. Cualquier regresión rompe CI de inmediato.

---

## Limitaciones

Ninguna de estas está oculta. Son los límites honestos de v0.1.0.

**Instalación PIE de extremo a extremo aún no verificada.**
El manifiesto `php-ext` en `composer.json` está listo y el comando `pie install webrek/tokenizers` es correcto, pero no se ha ejecutado en una máquina limpia (`pie` no estaba disponible en el entorno de desarrollo). Usa el camino "Instalación desde el código fuente" en [getting-started.md](getting-started.md) como método verificado.

**La normalización WordPiece es solo Latin-1 + espaciado CJK.**
La implementación maneja el plegado de caracteres Latin-1 y el espaciado de codepoints CJK (relleno con espacios), pero no realiza descomposición NFD completa de Unicode. Los scripts no latinos que dependen de la normalización NFD quedan fuera del alcance de v1. Los tokens para dichas entradas pueden diferir de la referencia de HuggingFace para caracteres fuera del rango cubierto.

**La normalización Unigram es Metaspace + identidad en ASCII.**
La implementación aplica Metaspace (`▁`, U+2581) y pasa el ASCII sin cambios. No realiza normalización NFKC. Algunos casos límite de espacios en blanco (espacios iniciales, espacios finales, múltiples espacios consecutivos) también pueden producir resultados que difieren de la referencia de SentencePiece.

**Claude 3+ y Gemini no tienen tokenizador local.**
Contar tokens para modelos Claude o Gemini requiere una llamada de red activa y una clave API válida. Los resultados son exactos solo según el tokenizador actual del proveedor; los proveedores pueden cambiar su tokenizador sin previo aviso.

**`Bpe::name()` devuelve `null` en v0.1.**
El seguimiento del nombre para instancias `Bpe` no está implementado. El método existe y devuelve `null`.

**Política OOM: el crash por OOM es aceptado en v1.**
Si el sistema se queda sin memoria durante una carga de vocabulario grande o una operación de codificación, PHP fallará en lugar de lanzar una excepción. Este es el comportamiento estándar de las extensiones PECL. Los sitios conocidos de realloc-leak fueron reforzados, pero los caminos de error seguros ante OOM completos se aplazan para una versión posterior.

**Las codificaciones heredadas de OpenAI `p50k_base` y `r50k_base` no están incluidas.**
Solo `cl100k_base` y `o200k_base` están integradas. `p50k_base` y `r50k_base` pueden añadirse en una versión futura; la arquitectura las soporta.

---

## Hoja de ruta

| Fase | Estado | Descripción |
|---|---|---|
| Fase 1 | Listo | BPE a nivel de byte: `cl100k_base`, `o200k_base`, BPE de HuggingFace mediante `tokenizer.json`. Fusión O(n log n). Conforme con Tiktoken. |
| Fase 2 | Listo | WordPiece (familia BERT) + Unigram (T5/SentencePiece). Exactitud a nivel de byte. `Encoding::fromHuggingFace()` despacha los tres algoritmos. |
| Fase 3 | Listo | Acompañante de API Claude/Gemini (PHP puro, independiente, sin dependencias de biblioteca HTTP). |
| Futuro | No construido | Publicación en PECL/PIE + binarios precompilados; codificaciones heredadas `p50k_base`/`r50k_base`; normalización NFD/NFKC completa; backlog de refuerzo ante OOM. |

Nota: versiones anteriores del README describían la Fase 2 y la Fase 3 como trabajo futuro. Eso era incorrecto. Las tres fases están completas en v0.1.0.

---

## Páginas relacionadas

- [getting-started.md](getting-started.md) — instalación, verificación, primera tokenización
- [api-reference.md](api-reference.md) — referencia completa de la API
- [guides/loading-models.md](guides/loading-models.md) — carga de modelos OpenAI y HuggingFace
- [guides/estimating-costs.md](guides/estimating-costs.md) — estimación de costos de API de LLM
- [guides/remote-providers.md](guides/remote-providers.md) — configuración del acompañante Claude/Gemini y limitaciones
