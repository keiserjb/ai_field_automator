<?php


namespace ai_field_automator\includes;

use AIContentExtractor;

/**
 * @file
 * Abstract base class for AI Field Automator plugins.
 */
abstract class AIFieldAutomatorBase implements
  AIFieldAutomatorInterface {

  /**
   * {@inheritdoc}
   */
  public function helpText() {
    return '';
  }

  /**
   * {@inheritdoc}
   */
  public function allowedInputs() {
    // Default: accept text-like fields as context.
    return ['text', 'text_long', 'text_with_summary', 'string', 'string_long'];
  }

  /**
   * {@inheritdoc}
   */
  public function ruleIsAllowed($entity, $field_name, array $config) {
    return TRUE;
  }

  /**
   * {@inheritdoc}
   */
  public function checkIfEmpty(array $value, array $config = []) {
    return $value;
  }

  /**
   * {@inheritdoc}
   */
  public function postCheckIfEmpty($entity, $value, array $config = []) {
    return $value;
  }

  /**
   * {@inheritdoc}
   */
  public function storeValues(
    $entity,
    array $values,
    $field_name,
    array $config
  ) {
    if (empty($values)) {
      return;
    }
    $field_info = field_info_field($field_name) ?: [];
    $primary_column = $this->getPrimaryColumn($field_info);
    $lang = LANGUAGE_NONE;

    // Build field items array.
    $items = [];
    foreach ($values as $delta => $value) {
      // Support both plain strings and arrays (e.g. text_with_summary).
      if (is_array($value)) {
        $items[$delta] = $value;
      }
      else {
        $items[$delta][$primary_column] = $value;
        // Carry over existing format for filtered text fields.
        if (!empty($field_info['settings']['allowed_formats'])) {
          $format = !empty($config['text_format']) ? $config['text_format']
            : filter_default_format();
          $items[$delta]['format'] = $format;
        }
      }
    }

    $entity->{$field_name}[$lang] = $items;
  }

  /**
   * {@inheritdoc}
   */
  public function verifyValue($entity, $value, $field_name, array $config) {
    return TRUE;
  }

  /**
   * Builds the prompt string from config, substituting simple {tokens}.
   *
   * Supported tokens:
   *   {title}              — entity label / title
   *   {field:FIELD_NAME}   — plain-text value of another field
   *
   * @param object $entity
   *   The entity being saved.
   * @param array  $config
   *   The rule config array.
   *
   * @return string
   *   The resolved prompt string.
   */
  protected function buildPrompt(
    $entity,
    array $config,
    $context_value = NULL
  ) {
    $prompt = $config['prompt'] ?? '';
    if (empty($prompt)) {
      return '';
    }

    // Replace {title} (tolerates optional spaces inside braces).
    // Use preg_replace only to normalize the brace variant, then str_replace for
    // the actual substitution — preg_replace treats $ in the replacement string
    // as a backreference, which corrupts values like "$100,000".
    $label = entity_label($config['entity_type'], $entity);
    $prompt = preg_replace('/\{\s*title\s*\}/', '{title}', $prompt);
    $prompt = str_replace('{title}', check_plain($label), $prompt);

    // Replace {context} (or { context } with optional spaces) with the explicit
    // context value, or resolve it from the configured base field. Same two-step
    // approach to avoid preg_replace backreference corruption.
    if ($context_value !== NULL) {
      $prompt = preg_replace('/\{\s*context\s*\}/', '{context}', $prompt);
      $prompt = str_replace('{context}', $context_value, $prompt);
    }
    elseif (!empty($config['base_field'])) {
      $base = $config['base_field'];
      $lang = LANGUAGE_NONE;
      $context_value = '';
      if ($base === '_title') {
        $context_value = check_plain($label);
      }
      elseif ($base === '_rendered_entity') {
        $context_value = $this->extractEntityContextText(
          $entity,
          $config['entity_type'] ?? ''
        );
      }
      elseif (isset($entity->{$base}[$lang][0])) {
        $item = $entity->{$base}[$lang][0];
        $context_value = AIContentExtractor::htmlToMarkdown($item['value'] ?? $item['safe_value'] ?? '');
      }
      $prompt = preg_replace('/\{\s*context\s*\}/', '{context}', $prompt);
      $prompt = str_replace('{context}', $context_value, $prompt);
    }

    // Replace {field:FIELD_NAME} tokens.
    preg_match_all('/\{field:([a-z0-9_]+)\}/', $prompt, $matches);
    foreach (($matches[1] ?? []) as $token_field) {
      $lang = LANGUAGE_NONE;
      $field_value = '';
      if (isset($entity->{$token_field}[$lang][0])) {
        $item = $entity->{$token_field}[$lang][0];
        $field_value = AIContentExtractor::htmlToMarkdown($item['value'] ?? $item['safe_value'] ?? '');
      }
      $prompt = str_replace(
        '{field:' . $token_field . '}',
        $field_value,
        $prompt
      );
    }

    return $prompt;
  }

  /**
   * Builds one or more prompts based on mode and field cardinality.
   *
   * Base mode:
   * - If the base field has multiple values, one prompt is built per delta.
   * Token mode:
   * - One token-resolved prompt is built.
   *
   * @param object $entity
   *   The entity being saved.
   * @param array  $config
   *   Automator config.
   *
   * @return array
   *   Array of prompt strings.
   */
  protected function buildPrompts($entity, array $config) {
    $mode = $config['mode'] ?? 'base';
    $entity_type = $config['entity_type'] ?? '';

    if ($mode === 'token') {
      $template = $config['token'] ?? '';
      if ($template === '') {
        return [];
      }
      $prompt = ai_field_automator_render_token_prompt(
        $template,
        $entity,
        $config['entity_type'] ?? ''
      );
      return $prompt !== '' ? [$prompt] : [];
    }

    $base_field = $config['base_field'] ?? '';
    if ($base_field === '_title') {
      $label = entity_label($config['entity_type'], $entity);
      $prompt = $this->buildPrompt($entity, $config, check_plain($label));
      return $prompt !== '' ? [$prompt] : [];
    }
    if ($base_field === '_rendered_entity') {
      $context = $this->extractEntityContextText(
        $entity,
        $entity_type,
        [$config['field_name'] ?? '']
      );
      $prompt = $this->buildPrompt(
        $entity,
        $config,
        $context !== '' ? $context : NULL
      );
      return $prompt !== '' ? [$prompt] : [];
    }

    $base_field_info = function_exists('field_info_field')
      ? field_info_field($base_field)
      : [];
    if (($base_field_info['type'] ?? '') === 'paragraphs'
      && !empty($entity->{$base_field}[LANGUAGE_NONE])
    ) {
      $context = $this->extractParagraphFieldText(
        $entity->{$base_field},
        $entity_type
      );
      $prompt = $this->buildPrompt(
        $entity,
        $config,
        $context !== '' ? $context : NULL
      );
      return $prompt !== '' ? [$prompt] : [];
    }

    if (empty($base_field) || !isset($entity->{$base_field}[LANGUAGE_NONE])) {
      $fallback_context = $this->extractEntityContextText(
        $entity,
        $entity_type
      );
      $prompt = $this->buildPrompt(
        $entity,
        $config,
        $fallback_context !== '' ? $fallback_context : NULL
      );
      return $prompt !== '' ? [$prompt] : [];
    }

    $prompts = [];
    foreach ($entity->{$base_field}[LANGUAGE_NONE] as $item) {
      $context = $this->extractItemText($item);
      if ($context === '') {
        continue;
      }
      $prompt = $this->buildPrompt($entity, $config, $context);
      if ($prompt !== '') {
        $prompts[] = $prompt;
      }
    }

    // If the selected base field doesn't yield text (common with paragraph-only
    // editing patterns), fall back to all entity text including paragraph items.
    if (empty($prompts)) {
      $fallback_context = $this->extractEntityContextText(
        $entity,
        $entity_type
      );
      if ($fallback_context !== '') {
        $prompt = $this->buildPrompt($entity, $config, $fallback_context);
        if ($prompt !== '') {
          $prompts[] = $prompt;
        }
      }
    }

    return $prompts;
  }

  /**
   * Extracts plain text from a paragraphs field only.
   */
  protected function extractParagraphFieldText(
    array $field_items,
    $entity_type = ''
  ) {
    $chunks = [];
    $visited_paragraph_ids = [];
    AIContentExtractor::collectParagraphItemsText(
      [LANGUAGE_NONE => $field_items[LANGUAGE_NONE] ?? $field_items],
      $chunks,
      $visited_paragraph_ids
    );

    $chunks = array_values(
      array_unique(array_filter(array_map('trim', $chunks)))
    );
    return implode("\n\n", $chunks);
  }

  /**
   * Extracts best-effort plain text from a field item array.
   *
   * @param array $item
   *   A field item.
   *
   * @return string
   *   Plain text value.
   */
  protected function extractItemText(array $item) {
    $chunks = [];
    AIContentExtractor::collectTextRecursive($item, $chunks);
    $chunks = array_values(array_unique(array_filter(array_map('trim', $chunks))));
    return implode("\n\n", $chunks);
  }

  /**
   * Extracts aggregate plain text context from an entity.
   *
   * For saved entities the preferred path is to render the entity to HTML and
   * strip the markup — this naturally handles paragraphs, avoids duplicated
   * content from body+paragraphs, and excludes image noise without needing
   * field-type-specific heuristics. The field-crawl fallback is kept for
   * unsaved/preview entities that cannot be rendered.
   */
  protected function extractEntityContextText($entity, $entity_type = '', array $exclude_fields = []) {
    return AIContentExtractor::extractEntityText(
      $entity_type,
      $entity,
      [
        'view_mode' => 'full',
        'preview_property' => 'ai_field_automator_preview_text',
        'include_label' => TRUE,
        'exclude_fields' => $exclude_fields,
      ]
    );
  }

  /**
   * Renders an entity to plain text, stripping images and other non-text
   * markup.
   *
   * @param object $entity
   *   A fully-loaded entity object.
   * @param string $entity_type
   *   The entity type (e.g. 'node').
   *
   * @return string
   *   Plain text, or empty string on failure.
   */
  protected function renderEntityToText($entity, $entity_type) {
    return AIContentExtractor::renderEntityToMarkdown(
      $entity_type,
      $entity,
      'full'
    );
  }

  /**
   * Collects text fragments from all field values on an entity.
   *
   * Used as fallback for unsaved entities that cannot be rendered via
   * entity_view(). For saved entities, renderEntityToText() is preferred.
   */
  protected function collectEntityTextFromFields(
    $entity,
    $entity_type,
    array &$chunks,
    array &$visited_paragraph_ids
  ) {
    AIContentExtractor::collectEntityFieldText(
      $entity_type,
      $entity,
      $chunks,
      $visited_paragraph_ids
    );
  }

  /**
   * Collects text from paragraph item references recursively.
   */
  protected function collectParagraphItemsText(
    array $field_items,
    array &$chunks,
    array &$visited_paragraph_ids
  ) {
    AIContentExtractor::collectParagraphItemsText(
      $field_items,
      $chunks,
      $visited_paragraph_ids
    );
  }

  /**
   * Recursively collects plain text fragments from nested array values.
   */
  protected function collectNestedText($value, array &$chunks) {
    AIContentExtractor::collectTextRecursive($value, $chunks);
  }

  /**
   * Returns the primary storage column for a field type.
   *
   * @param array $field_info
   *   Field info from field_info_field().
   *
   * @return string
   *   Primary column name, defaulting to "value".
   */
  protected function getPrimaryColumn(?array $field_info = []) {
    if (!empty($field_info['columns']) && is_array($field_info['columns'])) {
      $keys = array_keys($field_info['columns']);
      if (!empty($keys[0])) {
        return $keys[0];
      }
    }
    return 'value';
  }

  /**
   * Calls ai_chat() with the configured provider/model and returns text.
   *
   * @param string $prompt
   *   The user prompt string.
   * @param array  $config
   *   Rule config (keys: model, temperature, frequency_penalty,
   *   presence_penalty, top_p, max_tokens, reasoning_effort, image_field).
   * @param array  $files
   *   Optional array of file objects for vision-capable models.
   *
   * @return string
   *   The AI-generated text, or empty string on error or guardrail block.
   */
  protected function callAI($prompt, array $config, array $files = []) {
    $model = $config['model'] ?? '';
    $temperature = (float) ($config['temperature'] ?? 0.7);
    $max_tokens = (int) ($config['max_tokens'] ?? 1024);

    if (empty($model) || empty($prompt)) {
      return '';
    }

    // Run guardrails ourselves, rather than relying solely on ai_chat()'s
    // internal alter hooks: automator output is saved straight into an
    // entity field, so we need to positively distinguish "blocked" from
    // "generated text" rather than risk a guardrail's blocked-message
    // string getting persisted as if it were real AI content. We pass
    // disable_guardrails to ai_chat() below so the check only runs once.
    $guardrails_available = function_exists('ai_guardrails_run');
    if ($guardrails_available) {
      $pre = ai_guardrails_run($prompt, 'pre', 'field_generation');
      if ($pre['blocked']) {
        watchdog('ai_field_automator', 'AI field generation skipped: prompt blocked by guardrail @id.', ['@id' => $pre['guardrail_id']], WATCHDOG_WARNING);
        return '';
      }
      $prompt = $pre['text'];
    }

    $content = [
      ['type' => 'text', 'text' => $prompt],
    ];

    if (!empty($files)) {
      foreach ($files as $file) {
        if (!$this->imageFileIsUsable($file)) {
          continue;
        }
        $path = backdrop_realpath($file->uri);
        if ($path && is_file($path)) {
          $data = base64_encode(file_get_contents($path));
          if ($data) {
            $mime = !empty($file->filemime) ? $file->filemime : 'image/jpeg';
            $url = 'data:' . $mime . ';base64,' . $data;
            $content[] = [
              'type' => 'image_url',
              'image_url' => ['url' => $url],
            ];
          }
        }
      }
    }

    $messages = [
      ['role' => 'user', 'content' => count($content) > 1 ? $content : $prompt],
    ];

    try {
      // For reasoning models, force temperature=1 (the only allowed value)
      // and pass reasoning_effort if set.
      $is_reasoning = function_exists(
          'ai_field_automator_is_reasoning_model'
        )
        && ai_field_automator_is_reasoning_model($model);

      if ($is_reasoning) {
        $temperature = 1.0;
        $reasoning_effort = $config['reasoning_effort'] ?? '';
        // Use the underlying API object directly to pass reasoning_effort.
        if (!empty($reasoning_effort)
          && function_exists(
            'ai_get_api_for_model'
          )
        ) {
          $stripped_model = '';
          $api = ai_get_api_for_model($model, $stripped_model);
          if ($api && method_exists($api, 'chatWithOptions')) {
            $response = $api->chatWithOptions($stripped_model, $messages, [
              'temperature'      => $temperature,
              'max_tokens'       => $max_tokens,
              'reasoning_effort' => $reasoning_effort,
              'operation'        => 'field_generation',
            ]);
            if (is_string($response) && trim($response) !== '') {
              return trim($response);
            }
          }
        }
      }

      $response = ai_chat(
        $model,
        $messages,
        $temperature,
        $max_tokens,
        FALSE,
        ['operation' => 'field_generation', 'disable_guardrails' => $guardrails_available]
      );

      $text = '';
      // All provider adapters return the content string directly.
      if (is_string($response) && trim($response) !== '') {
        $text = trim($response);
      }
      // Fallback: handle raw array format from any adapter that may return it.
      elseif (is_array($response)
        && !empty($response['choices'][0]['message']['content'])
      ) {
        $text = trim($response['choices'][0]['message']['content']);
      }

      if ($text === '') {
        return '';
      }

      if ($guardrails_available) {
        $post = ai_guardrails_run($text, 'post', 'field_generation');
        if ($post['blocked']) {
          watchdog('ai_field_automator', 'AI field generation discarded: response blocked by guardrail @id.', ['@id' => $post['guardrail_id']], WATCHDOG_WARNING);
          return '';
        }
        $text = $post['text'];
      }

      return $text;
    }
    catch (\Exception $e) {
      watchdog(
        'ai_field_automator',
        'AI call failed: @msg',
        ['@msg' => $e->getMessage()],
        WATCHDOG_ERROR
      );
    }

    return '';
  }

  /**
   * Calls the model with tool support when the registry is available.
   *
   * @param array $messages
   *   AI-standard chat messages.
   * @param array $tools
   *   Either tool names from ai_tools or ready-made tool definitions.
   * @param array $config
   *   Rule config.
   * @param int $max_rounds
   *   Maximum tool-call rounds before giving up.
   *
   * @return string
   *   Final assistant text, or empty string on failure.
   */
  protected function callAIWithTools(array $messages, array $tools, array $config, $max_rounds = 3) {
    $model = $config['model'] ?? '';
    $temperature = (float) ($config['temperature'] ?? 0.7);
    $max_tokens = (int) ($config['max_tokens'] ?? 1024);

    if (empty($model) || empty($messages)) {
      return '';
    }

    if (!module_exists('ai_tools') || !function_exists('ai_tools_get_tools')) {
      $last = end($messages);
      return $this->callAI((string) ($last['content'] ?? ''), $config);
    }

    // Fallback if the provider doesn't support tools.
    if (function_exists('ai_get_api_for_model')) {
      $stripped_model = '';
      $api = ai_get_api_for_model($model, $stripped_model);
      if ($api && method_exists($api, 'getModelsByCapability')) {
        $tool_models = $api->getModelsByCapability('tools');
        if (!isset($tool_models[$stripped_model])) {
          $last = end($messages);
          return $this->callAI((string) ($last['content'] ?? ''), $config);
        }
      }
      elseif ($api && !method_exists($api, 'chatWithTools')) {
        // Direct check if chatWithTools isn't even on the AIApi object
        // (though it usually is, we should be safe).
        $last = end($messages);
        return $this->callAI((string) ($last['content'] ?? ''), $config);
      }
    }

    $tool_registry = ai_tools_get_tools();
    $tool_definitions = [];
    foreach ($tools as $tool) {
      if (is_string($tool)) {
        if (!empty($tool_registry[$tool]['definition'])) {
          $tool_definitions[] = $tool_registry[$tool]['definition'];
        }
        continue;
      }
      if (!empty($tool['definition'])) {
        $tool_definitions[] = $tool['definition'];
      }
      elseif (!empty($tool['function']['name'])) {
        $tool_definitions[] = $tool;
      }
    }

    if (empty($tool_definitions) || !function_exists('ai_get_api_for_model')) {
      $last = end($messages);
      return $this->callAI((string) ($last['content'] ?? ''), $config);
    }

    $stripped_model = '';
    $api = ai_get_api_for_model($model, $stripped_model);
    if (!$api || !method_exists($api, 'chatWithTools')) {
      $last = end($messages);
      return $this->callAI((string) ($last['content'] ?? ''), $config);
    }

    for ($round = 0; $round < max(1, (int) $max_rounds); $round++) {
      $result = $api->chatWithTools(
        $stripped_model ?: $model,
        $messages,
        $tool_definitions,
        $temperature,
        $max_tokens,
        'auto',
        ['operation' => 'field_generation']
      );

      if (!is_array($result)) {
        return '';
      }

      $finish_reason = isset($result['finish_reason']) ? (string) $result['finish_reason'] : 'stop';
      $content = isset($result['content']) ? trim((string) $result['content']) : '';
      if ($finish_reason !== 'tool_calls' || empty($result['tool_calls']) || !is_array($result['tool_calls'])) {
        return $content;
      }

      $assistant_tool_calls = [];
      foreach ($result['tool_calls'] as $tool_call) {
        $assistant_tool_calls[] = [
          'id' => isset($tool_call['id']) ? (string) $tool_call['id'] : '',
          'type' => 'function',
          'function' => [
            'name' => isset($tool_call['name']) ? (string) $tool_call['name'] : '',
            'arguments' => json_encode(isset($tool_call['arguments']) ? $tool_call['arguments'] : []),
          ],
        ];
      }

      $messages[] = [
        'role' => 'assistant',
        'content' => $content !== '' ? $content : NULL,
        'tool_calls' => $assistant_tool_calls,
      ];

      foreach ($result['tool_calls'] as $tool_call) {
        $tool_name = isset($tool_call['name']) ? (string) $tool_call['name'] : '';
        $tool_args = isset($tool_call['arguments']) && is_array($tool_call['arguments']) ? $tool_call['arguments'] : [];
        $messages[] = [
          'role' => 'tool',
          'tool_call_id' => isset($tool_call['id']) ? (string) $tool_call['id'] : '',
          'content' => ai_tools_dispatch($tool_name, $tool_args),
        ];
      }
    }

    return '';
  }

  /**
   * Decodes AI text response into JSON array payload.
   *
   * Supports fenced code blocks and plain JSON arrays/objects.
   *
   * @param string $text
   *   Raw AI response text.
   *
   * @return array
   *   Normalized list of items.
   */
  protected function decodeJsonPayload($text) {
    if (!is_string($text) || trim($text) === '') {
      return [];
    }

    $clean = trim(str_replace(['```json', '```'], '', $text));
    // Extract first JSON array or object when preamble text precedes it.
    if (!in_array($clean[0] ?? '', ['{', '['], TRUE)) {
      if (preg_match('/(\[[\s\S]*\]|\{[\s\S]*\})/u', $clean, $m)) {
        $clean = $m[1];
      }
    }
    $decoded = json_decode($clean, TRUE);
    if (!is_array($decoded)) {
      return [];
    }

    // Normalize object to a single-item list.
    if (isset($decoded['value']) || isset($decoded['uri'])
      || isset($decoded['title'])
    ) {
      return [$decoded];
    }

    return $decoded;
  }

  /**
   * Creates and saves a managed file from binary data.
   *
   * @param string $binary
   *   File binary data.
   * @param string $filename
   *   Target filename.
   *
   * @return object|null
   *   Saved file entity-like object with fid, or NULL on failure.
   */
  protected function createManagedFile($binary, $filename) {
    if (!is_string($binary) || $binary === '') {
      return NULL;
    }

    // Filename patterns are bare filenames; strip any path segments so a
    // crafted value can't write outside the files directory.
    $filename = basename(str_replace('\\', '/', (string) $filename));
    if ($filename === '' || $filename === '.' || $filename === '..') {
      $filename = 'ai-file-' . REQUEST_TIME;
    }

    $uri = file_unmanaged_save_data(
      $binary,
      file_default_scheme() . '://' . $filename,
      FILE_EXISTS_RENAME
    );
    if (!$uri) {
      return NULL;
    }

    $file = entity_create('file', [
      'uid'       => $GLOBALS['user']->uid ?? 0,
      'status'    => 1,
      'filename'  => basename($uri),
      'uri'       => $uri,
      'filemime'  => file_get_mimetype($uri),
      'timestamp' => REQUEST_TIME,
    ]);
    file_save($file);
    // Mirror core managed file uploads so file widget validation accepts the
    // generated permanent file before the host entity is saved.
    file_usage_add($file, 'file', 'file', $file->fid);
    return $file;
  }

  /**
   * Generate speech audio, chunking long text when provider limits require it.
   */
  protected function generateTextToSpeechBinary(
    $api,
    $api_model,
    $source_model,
    $text,
    $voice,
    $format
  ) {
    $text = trim((string) $text);
    if ($text === '' || !$api || !method_exists($api, 'textToSpeech')) {
      return '';
    }

    $chunks = $this->chunkTextForTextToSpeech(
      $text,
      $this->getTextToSpeechCharacterLimit($source_model, $format)
    );
    if (empty($chunks)) {
      return '';
    }

    $segments = [];
    foreach ($chunks as $chunk) {
      $audio = $api->textToSpeech($api_model, $chunk, $voice, $format);
      if (!is_string($audio) || $audio === '') {
        return '';
      }
      $segments[] = $audio;
    }

    if (count($segments) === 1) {
      return $segments[0];
    }

    $combined = $this->combineTextToSpeechSegments($segments, $format);
    if ($combined !== '') {
      watchdog(
        'ai_field_automator',
        'Combined @count TTS chunks into one @format file.',
        [
          '@count' => count($segments),
          '@format' => $format,
        ],
        WATCHDOG_INFO
      );
      return $combined;
    }

    watchdog(
      'ai_field_automator',
      'Failed to combine @count TTS chunks; falling back to first segment only.',
      ['@count' => count($segments)],
      WATCHDOG_WARNING
    );
    return $segments[0];
  }

  /**
   * Split text into provider-safe TTS chunks.
   */
  protected function chunkTextForTextToSpeech($text, $max_chars = 0) {
    $text = trim((string) $text);
    if ($text === '') {
      return [];
    }
    if ($max_chars <= 0 || mb_strlen($text) <= $max_chars) {
      return [$text];
    }

    $chunks = [];
    while (mb_strlen($text) > $max_chars) {
      $slice = mb_substr($text, 0, $max_chars + 1);
      $breakpoint = $this->findTextToSpeechBreakpoint($slice, $max_chars);
      if ($breakpoint <= 0) {
        $breakpoint = $max_chars;
      }

      $chunk = trim(mb_substr($text, 0, $breakpoint));
      if ($chunk === '') {
        $chunk = trim(mb_substr($text, 0, $max_chars));
        $breakpoint = $max_chars;
      }

      $chunks[] = $chunk;
      $text = trim(mb_substr($text, $breakpoint));
    }

    if ($text !== '') {
      $chunks[] = $text;
    }

    return $chunks;
  }

  /**
   * Pick a natural breakpoint inside a TTS chunk.
   */
  protected function findTextToSpeechBreakpoint($text, $max_chars) {
    $needles = ["\n\n", "\n", '. ', '! ', '? ', '; ', ': ', ', ', ' '];
    foreach ($needles as $needle) {
      $pos = mb_strrpos($text, $needle, 0, 'UTF-8');
      if ($pos !== FALSE && $pos > 0 && $pos <= $max_chars) {
        return $pos + mb_strlen($needle);
      }
    }
    return $max_chars;
  }

  /**
   * Return provider-specific TTS text limit.
   */
  protected function getTextToSpeechCharacterLimit($model, $format = 'mp3') {
    $model = strtolower((string) $model);
    if ($format === 'pcm') {
      return 0;
    }
    if ($model === 'ai' || strpos($model, 'openai/') === 0) {
      return 4096;
    }
    return 0;
  }

  /**
   * Combine multiple audio binaries into one file.
   */
  protected function combineTextToSpeechSegments(array $segments, $format) {
    $segments = array_values(array_filter($segments, function($segment) {
      return is_string($segment) && $segment !== '';
    }));
    if (empty($segments)) {
      return '';
    }
    if (count($segments) === 1) {
      return $segments[0];
    }
    if ($format === 'pcm') {
      return implode('', $segments);
    }
    $fallback = $this->combineTextToSpeechSegmentsWithoutFfmpeg($segments, $format);
    if (!$this->isFfmpegAvailable()) {
      return $fallback;
    }

    $tmp_dir = file_directory_temp() . '/ai_field_automator_tts_' . uniqid();
    if (!file_prepare_directory($tmp_dir, FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS)) {
      return '';
    }

    $segment_paths = [];
    foreach ($segments as $delta => $segment) {
      $segment_path = $tmp_dir . '/segment_' . $delta . '.' . $format;
      if (@file_put_contents($segment_path, $segment) !== FALSE) {
        $segment_paths[] = $segment_path;
      }
    }

    if (count($segment_paths) !== count($segments)) {
      $this->deleteTmpDir($tmp_dir);
      return '';
    }

    $final_path = $tmp_dir . '/combined.' . $format;
    $command = $this->buildTextToSpeechConcatCommand($segment_paths, $final_path, $format);
    $output = @shell_exec($command . ' 2>&1');

    $binary = '';
    if (is_file($final_path) && filesize($final_path) > 0) {
      $binary = (string) @file_get_contents($final_path);
    }
    elseif (!empty($output)) {
      watchdog(
        'ai_field_automator',
        'TTS chunk combine failed for @format output: @error',
        [
          '@format' => $format,
          '@error' => trim(substr((string) $output, -1000)),
        ],
        WATCHDOG_WARNING
      );
    }
    if ($binary === '' && $fallback !== '') {
      $binary = $fallback;
    }

    $this->deleteTmpDir($tmp_dir);
    return $binary;
  }

  /**
   * Combine segments without ffmpeg when the format permits simple joining.
   */
  protected function combineTextToSpeechSegmentsWithoutFfmpeg(array $segments, $format) {
    switch (strtolower((string) $format)) {
      case 'mp3':
        return $this->combineMp3Segments($segments);

      default:
        return '';
    }
  }

  /**
   * Join MP3 segments, stripping repeated ID3v2 tags from later chunks.
   */
  protected function combineMp3Segments(array $segments) {
    $combined = '';
    foreach (array_values($segments) as $delta => $segment) {
      if ($delta > 0) {
        $segment = $this->stripMp3Id3v2Tag($segment);
      }
      $combined .= $segment;
    }
    return $combined;
  }

  /**
   * Remove an ID3v2 header from the start of an MP3 binary.
   */
  protected function stripMp3Id3v2Tag($binary) {
    if (strncmp($binary, 'ID3', 3) !== 0 || strlen($binary) < 10) {
      return $binary;
    }

    $size_bytes = substr($binary, 6, 4);
    $size = ((ord($size_bytes[0]) & 0x7F) << 21)
      | ((ord($size_bytes[1]) & 0x7F) << 14)
      | ((ord($size_bytes[2]) & 0x7F) << 7)
      | (ord($size_bytes[3]) & 0x7F);
    $header_size = 10 + $size;

    if (!empty(ord($binary[5]) & 0x10)) {
      $header_size += 10;
    }

    return substr($binary, $header_size);
  }

  /**
   * Build the ffmpeg command used to merge TTS segments.
   */
  protected function buildTextToSpeechConcatCommand(array $segment_paths, $final_path, $format) {
    $inputs = [];
    $filter_inputs = [];
    foreach (array_values($segment_paths) as $delta => $segment_path) {
      $inputs[] = '-i ' . escapeshellarg($segment_path);
      $filter_inputs[] = '[' . $delta . ':a]';
    }

    return 'ffmpeg -y -nostdin '
      . implode(' ', $inputs)
      . ' -filter_complex '
      . escapeshellarg(implode('', $filter_inputs) . 'concat=n=' . count($segment_paths) . ':v=0:a=1[a]')
      . ' -map ' . escapeshellarg('[a]')
      . ' -vn ' . $this->getTextToSpeechConcatArgs($format)
      . ' ' . escapeshellarg($final_path);
  }

  /**
   * Return ffmpeg codec args for merged TTS output.
   */
  protected function getTextToSpeechConcatArgs($format) {
    switch (strtolower((string) $format)) {
      case 'mp3':
        return '-c:a libmp3lame';

      case 'aac':
        return '-c:a aac';

      case 'opus':
        return '-c:a libopus';

      case 'flac':
        return '-c:a flac';

      case 'wav':
        return '-c:a pcm_s16le';

      default:
        return '-c copy';
    }
  }

  /**
   * Checks if ffmpeg is available.
   */
  protected function isFfmpegAvailable() {
    $which = @shell_exec('which ffmpeg 2>/dev/null');
    return !empty(trim((string) $which));
  }

  /**
   * Delete a temporary directory recursively.
   */
  protected function deleteTmpDir($path) {
    if (empty($path) || !is_dir($path)) {
      return;
    }
    $items = scandir($path);
    if (!is_array($items)) {
      return;
    }
    foreach ($items as $item) {
      if ($item === '.' || $item === '..') {
        continue;
      }
      $item_path = $path . '/' . $item;
      if (is_dir($item_path)) {
        $this->deleteTmpDir($item_path);
      }
      else {
        @unlink($item_path);
      }
    }
    @rmdir($path);
  }

  /**
   * Loads image/file metadata from an entity field for vision-capable models.
   *
   * @param object $entity
   *   The entity object.
   * @param string $field_name
   *   The field name to extract images from.
   *
   * @return array
   *   Array of file objects.
   */
  protected function getImagesFromField($entity, $field_name) {
    if (empty($field_name) || empty($entity->{$field_name})) {
      return [];
    }

    $files = [];
    $lang = LANGUAGE_NONE;
    if (!empty($entity->{$field_name}[$lang])) {
      foreach ($entity->{$field_name}[$lang] as $item) {
        if (!empty($item['fid'])) {
          $file = file_load($item['fid']);
          if ($file && $this->imageFileIsUsable($file)) {
            $files[] = $file;
          }
        }
      }
    }

    return $files;
  }

  /**
   * Loads image/file metadata from an entity field keyed by field delta.
   *
   * @param object $entity
   *   The entity object.
   * @param string $field_name
   *   The field name to extract images from.
   *
   * @return array
   *   Array of file objects keyed by source field delta.
   */
  protected function getImagesFromFieldByDelta($entity, $field_name) {
    if (empty($field_name) || empty($entity->{$field_name})) {
      return [];
    }

    $files = [];
    if (!empty($entity->{$field_name}[LANGUAGE_NONE])) {
      foreach ($entity->{$field_name}[LANGUAGE_NONE] as $delta => $item) {
        if (empty($item['fid'])) {
          continue;
        }
        $file = file_load($item['fid']);
        if ($file && $this->imageFileIsUsable($file)) {
          $files[$delta] = $file;
        }
      }
    }

    return $files;
  }

  /**
   * Checks whether a file can be sent to a vision model.
   */
  protected function imageFileIsUsable($file) {
    if (empty($file->uri) || empty($file->filemime)) {
      return FALSE;
    }
    if (strpos($file->filemime, 'image/') !== 0) {
      return FALSE;
    }
    if (function_exists('file_access') && !file_access('view', $file)) {
      return FALSE;
    }
    $path = backdrop_realpath($file->uri);
    if (!$path || !is_file($path)) {
      return FALSE;
    }
    $size = filesize($path);
    return $size !== FALSE && $size <= 20 * 1024 * 1024;
  }

}
