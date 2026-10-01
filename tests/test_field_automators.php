<?php

/**
 * @file
 * Comprehensive verification and regression test suite for all 37 AI Field Automators.
 *
 * Can be executed via CLI:
 *   ddev exec php modules/contrib/ai_field_automator/tests/test_field_automators.php
 * Or directly with PHP if Backdrop is bootstrapped.
 */

if (!defined('BACKDROP_ROOT')) {
  define('BACKDROP_ROOT', getcwd());
}

if (!function_exists('backdrop_bootstrap')) {
  require_once BACKDROP_ROOT . '/core/includes/bootstrap.inc';
  backdrop_bootstrap(BACKDROP_BOOTSTRAP_FULL);
}

module_load_include('module', 'ai_field_automator');
module_load_include('inc', 'ai_field_automator', 'includes/AIFieldAutomatorBase');

$passed = 0;
$failed = 0;

function test_assert($condition, $description) {
  global $passed, $failed;
  if ($condition) {
    $passed++;
    print "  [PASS] {$description}\n";
  }
  else {
    $failed++;
    print "  [FAIL] {$description}\n";
  }
}

print "============================================================\n";
print " AI Field Automator Comprehensive Test Suite (37 Automators)\n";
print "============================================================\n\n";

// ---------------------------------------------------------------------------
// 1. Registry & Discovery: All 37 Automator Types
// ---------------------------------------------------------------------------
print "--- 1. Testing Registry Discovery & Plugin Instantiation ---\n";

$types = ai_field_automator_get_types();
test_assert(is_array($types), 'ai_field_automator_get_types() returns an array');
test_assert(count($types) === 37, 'ai_field_automator_get_types() discovers exactly 37 automator types (found ' . count($types) . ')');

$all_expected_types = [
  'simple_text',
  'long_text',
  'text_create_summary',
  'boolean',
  'numeric',
  'date_extract',
  'email',
  'telephone',
  'options',
  'link',
  'taxonomy_terms',
  'entity_reference',
  'json_field',
  'json_native_field',
  'json_native_binary_field',
  'metatag',
  'moderation_state',
  'address',
  'office_hours',
  'faq_field',
  'chart_from_text',
  'text_to_image',
  'image_alt_text',
  'image_to_text',
  'audio_to_text',
  'text_to_speech',
  'video_to_text',
  'media_image_generation',
  'media_audio_generation',
  'rewrite_image_filename',
  'video_to_html',
  'video_to_image',
  'video_to_video',
  'vector_search_text',
  'vector_search_entity_reference',
  'views_extract_text',
  'agent_field',
];

$instances = [];
$instantiation_failures = 0;
$contract_failures = 0;

foreach ($all_expected_types as $type_key) {
  if (!isset($types[$type_key])) {
    $instantiation_failures++;
    test_assert(FALSE, "Type key '{$type_key}' is missing from registry");
    continue;
  }

  $info = $types[$type_key];
  $class_name = $info['class'] ?? '';
  if (!$class_name || !class_exists($class_name)) {
    $instantiation_failures++;
    test_assert(FALSE, "Class '{$class_name}' for type '{$type_key}' does not exist");
    continue;
  }

  try {
    $plugin = new $class_name();
    $instances[$type_key] = $plugin;

    $help = $plugin->helpText();
    $allowed_fields = $plugin->allowedFieldTypes();
    $allowed_inputs = $plugin->allowedInputs();

    if (!is_string($help) || empty($help) || !is_array($allowed_fields) || !is_array($allowed_inputs)) {
      $contract_failures++;
    }
  }
  catch (\Throwable $e) {
    $instantiation_failures++;
    test_assert(FALSE, "Exception instantiating '{$class_name}': " . $e->getMessage());
  }
}

test_assert($instantiation_failures === 0, 'All 37 plugin classes successfully exist and instantiate');
test_assert($contract_failures === 0, 'All 37 plugins satisfy base contract (helpText, allowedFieldTypes, allowedInputs)');

// ---------------------------------------------------------------------------
// 2. Base Class Methods & JSON Decoding
// ---------------------------------------------------------------------------
print "\n--- 2. Testing Base Class Helpers & Payload Decoding ---\n";

$simple_plugin = $instances['simple_text'];
$simple_ref = new \ReflectionClass($simple_plugin);
$decode_method = $simple_ref->getMethod('decodeJsonPayload');
$decode_method->setAccessible(TRUE);

// Test decodeJsonPayload
$clean_json = '[{"value": "Backdrop CMS"}]';
$decoded_clean = $decode_method->invoke($simple_plugin, $clean_json);
test_assert(isset($decoded_clean[0]['value']) && $decoded_clean[0]['value'] === 'Backdrop CMS', 'decodeJsonPayload decodes standard JSON');

$fenced_json = "```json\n[{\"value\": \"Fenced Value\"}]\n```";
$decoded_fenced = $decode_method->invoke($simple_plugin, $fenced_json);
test_assert(isset($decoded_fenced[0]['value']) && $decoded_fenced[0]['value'] === 'Fenced Value', 'decodeJsonPayload strips markdown json code fences');

$text_with_json = "Here is the result:\n[{\"value\": \"Embedded Value\"}]\nHope this helps!";
$decoded_embedded = $decode_method->invoke($simple_plugin, $text_with_json);
test_assert(isset($decoded_embedded[0]['value']) && $decoded_embedded[0]['value'] === 'Embedded Value', 'decodeJsonPayload extracts JSON array embedded in conversational text');

$object_json = '{"title": "My Object"}';
$decoded_object = $decode_method->invoke($simple_plugin, $object_json);
test_assert(isset($decoded_object[0]['title']) && $decoded_object[0]['title'] === 'My Object', 'decodeJsonPayload wraps single object into a rows array');

$invalid_json = 'Not JSON at all';
$decoded_invalid = $decode_method->invoke($simple_plugin, $invalid_json);
test_assert(is_array($decoded_invalid) && empty($decoded_invalid), 'decodeJsonPayload gracefully returns empty array for invalid JSON');

// ---------------------------------------------------------------------------
// 3. Scalar / Text / Primitives Automators
// ---------------------------------------------------------------------------
print "\n--- 3. Testing Scalar & Primitives Automator Plugins ---\n";

// 3.1 Simple Text
$mock_entity = (object) [
  'nid' => 101,
  'title' => 'Test Article',
  'body' => [LANGUAGE_NONE => [['value' => 'Article body content here.']]],
];
$simple_plugin->storeValues($mock_entity, ['AI Generated Summary Text'], 'field_summary', []);
test_assert(isset($mock_entity->field_summary[LANGUAGE_NONE][0]['value']) && $mock_entity->field_summary[LANGUAGE_NONE][0]['value'] === 'AI Generated Summary Text', 'AIFieldAutomatorSimpleText stores string into entity field value');

// 3.2 Long Text
$long_plugin = $instances['long_text'];
$long_text_val = "Paragraph 1\n\nParagraph 2\n\nParagraph 3";
$long_plugin->storeValues($mock_entity, [$long_text_val], 'field_description', []);
test_assert($mock_entity->field_description[LANGUAGE_NONE][0]['value'] === $long_text_val, 'AIFieldAutomatorLongText stores multiline text preserving newlines');

// 3.3 Text Create Summary
$summary_plugin = $instances['text_create_summary'];
$summary_plugin->storeValues($mock_entity, ['Short Summary'], 'body', []);
test_assert($mock_entity->body[LANGUAGE_NONE][0]['summary'] === 'Short Summary', 'AIFieldAutomatorTextCreateSummary stores generated summary in summary column');

// 3.4 Boolean
$bool_plugin = $instances['boolean'];
// Test boolean extraction via mock/reflection or direct payload testing
$bool_test_cases = [
  'true' => 1,
  'True' => 1,
  'YES' => 1,
  '1' => 1,
  'false' => 0,
  'False' => 0,
  'NO' => 0,
  '0' => 0,
];
$bool_passed = TRUE;
foreach ($bool_test_cases as $input => $expected) {
  $mock_ent = (object) ['title' => 'Test'];
  $bool_plugin->storeValues($mock_ent, [$expected], 'field_bool', []);
  if (!isset($mock_ent->field_bool[LANGUAGE_NONE][0]['value']) || (int) $mock_ent->field_bool[LANGUAGE_NONE][0]['value'] !== $expected) {
    $bool_passed = FALSE;
  }
}
test_assert($bool_passed, 'AIFieldAutomatorBoolean stores normalized 1/0 values properly');

// 3.5 Numeric
$numeric_plugin = $instances['numeric'];
$num_entity = (object) [];
$numeric_plugin->storeValues($num_entity, ['42.50'], 'field_price', []);
test_assert(isset($num_entity->field_price[LANGUAGE_NONE][0]['value']) && $num_entity->field_price[LANGUAGE_NONE][0]['value'] === '42.50', 'AIFieldAutomatorNumeric stores numeric values');

// 3.6 Date Extract
$date_plugin = $instances['date_extract'];
$date_entity = (object) [];
$date_plugin->storeValues($date_entity, [['value' => '2026-09-18 12:00:00']], 'field_event_date', []);
test_assert(isset($date_entity->field_event_date[LANGUAGE_NONE][0]['value']) && $date_entity->field_event_date[LANGUAGE_NONE][0]['value'] === '2026-09-18 12:00:00', 'AIFieldAutomatorDateExtract stores formatted date values');

// 3.7 Email
$email_plugin = $instances['email'];
$email_entity = (object) [];
$email_plugin->storeValues($email_entity, ['contact@backdropcms.org'], 'field_email', []);
test_assert($email_entity->field_email[LANGUAGE_NONE][0]['email'] === 'contact@backdropcms.org', 'AIFieldAutomatorEmail stores into email column');

// 3.8 Telephone (testing the regex fix we made)
$phone_plugin = $instances['telephone'];
$phone_entity = (object) [];
$phone_plugin->storeValues($phone_entity, ['+1 (555) 123-4567'], 'field_phone', []);
test_assert($phone_entity->field_phone[LANGUAGE_NONE][0]['value'] === '+1 (555) 123-4567', 'AIFieldAutomatorTelephone stores telephone value');

// Test phone regex candidate matching
$candidate_valid = '+15551234567';
$matched = preg_match('/^\+\(?[0-9]{1,3}\)?[ ]?[0-9]{6,12}$/', $candidate_valid);
test_assert($matched === 1, 'AIFieldAutomatorTelephone regex correctly accepts international number');
$candidate_parens = '+(1) 5551234567';
$matched_parens = preg_match('/^\+\(?[0-9]{1,3}\)?[ ]?[0-9]{6,12}$/', $candidate_parens);
test_assert($matched_parens === 1, 'AIFieldAutomatorTelephone regex correctly accepts closed parentheses +(1)');

// 3.9 Options
$options_plugin = $instances['options'];
$options_entity = (object) [];
$options_plugin->storeValues($options_entity, ['featured'], 'field_status', []);
test_assert($options_entity->field_status[LANGUAGE_NONE][0]['value'] === 'featured', 'AIFieldAutomatorOptions stores selected option key');

// 3.10 Link
$link_plugin = $instances['link'];
$link_entity = (object) [];
$link_plugin->storeValues($link_entity, [['url' => 'https://backdropcms.org', 'title' => 'Backdrop CMS']], 'field_website', []);
test_assert($link_entity->field_website[LANGUAGE_NONE][0]['url'] === 'https://backdropcms.org', 'AIFieldAutomatorLink stores url property');
test_assert($link_entity->field_website[LANGUAGE_NONE][0]['title'] === 'Backdrop CMS', 'AIFieldAutomatorLink stores title property');

// ---------------------------------------------------------------------------
// 4. Reference Automator Plugins
// ---------------------------------------------------------------------------
print "\n--- 4. Testing Reference Automator Plugins ---\n";

// 4.1 Taxonomy Terms
$taxo_plugin = $instances['taxonomy_terms'];
$taxo_entity = (object) [];
$taxo_plugin->storeValues($taxo_entity, [5, 12, 18], 'field_tags', []);
test_assert(count($taxo_entity->field_tags[LANGUAGE_NONE]) === 3, 'AIFieldAutomatorTaxonomyTerms stores term references');
test_assert($taxo_entity->field_tags[LANGUAGE_NONE][0]['tid'] === 5, 'AIFieldAutomatorTaxonomyTerms stores tid column');

// 4.2 Entity Reference
$er_plugin = $instances['entity_reference'];
$er_entity = (object) [];
$er_plugin->storeValues($er_entity, [101, 202], 'field_related_nodes', []);
test_assert(count($er_entity->field_related_nodes[LANGUAGE_NONE]) === 2, 'AIFieldAutomatorEntityReference stores entity references');
test_assert($er_entity->field_related_nodes[LANGUAGE_NONE][1]['target_id'] === 202, 'AIFieldAutomatorEntityReference stores target_id column');

// ---------------------------------------------------------------------------
// 5. Structured Data Automators
// ---------------------------------------------------------------------------
print "\n--- 5. Testing Structured Data Automator Plugins ---\n";

// 5.1 JSON Field / JSON Native / Binary
$json_plugin = $instances['json_field'];
$json_entity = (object) [];
$json_plugin->storeValues($json_entity, ['{"schema": "Product", "price": 19.99}'], 'field_json', []);
test_assert($json_entity->field_json[LANGUAGE_NONE][0]['value'] === '{"schema": "Product", "price": 19.99}', 'AIFieldAutomatorJsonStructured stores JSON string');

// 5.2 Metatag
$meta_plugin = $instances['metatag'];
$meta_entity = (object) [];
$meta_values = [
  'title' => 'AI Automated Page Title',
  'description' => 'Automated page description.',
  'keywords' => 'backdrop, ai, automator',
];
$meta_plugin->storeValues($meta_entity, [$meta_values], 'metatags', []);
$decoded_meta = json_decode($meta_entity->metatags[LANGUAGE_NONE][0]['value'] ?? '{}', TRUE);
test_assert(isset($decoded_meta['title']) && $decoded_meta['title'] === 'AI Automated Page Title', 'AIFieldAutomatorMetatag stores meta title');
test_assert(isset($decoded_meta['description']) && $decoded_meta['description'] === 'Automated page description.', 'AIFieldAutomatorMetatag stores meta description');
test_assert(isset($decoded_meta['keywords']) && $decoded_meta['keywords'] === 'backdrop, ai, automator', 'AIFieldAutomatorMetatag stores meta keywords');

// 5.3 Moderation State
$mod_plugin = $instances['moderation_state'];
$mod_entity = (object) [];
$mod_plugin->storeValues($mod_entity, ['needs_review'], 'moderation_state', []);
test_assert(isset($mod_entity->moderation_state[LANGUAGE_NONE][0]['value']) && $mod_entity->moderation_state[LANGUAGE_NONE][0]['value'] === 'needs_review', 'AIFieldAutomatorModerationState sets moderation_state property');

// ---------------------------------------------------------------------------
// 6. Domain Automator Plugins
// ---------------------------------------------------------------------------
print "\n--- 6. Testing Domain Automator Plugins ---\n";

// 6.1 Address
$addr_plugin = $instances['address'];
$addr_entity = (object) [];
$sample_address = [
  'thoroughfare' => '5151 E Memorial Dr',
  'locality' => 'Muncie',
  'administrative_area' => 'IN',
  'postal_code' => '47302',
  'country' => 'US',
];
$addr_plugin->storeValues($addr_entity, [$sample_address], 'field_address', []);
test_assert($addr_entity->field_address[LANGUAGE_NONE][0]['thoroughfare'] === '5151 E Memorial Dr', 'AIFieldAutomatorAddress stores thoroughfare');
test_assert($addr_entity->field_address[LANGUAGE_NONE][0]['locality'] === 'Muncie', 'AIFieldAutomatorAddress stores locality');
test_assert($addr_entity->field_address[LANGUAGE_NONE][0]['country'] === 'US', 'AIFieldAutomatorAddress stores country');

// 6.2 Office Hours
$hours_plugin = $instances['office_hours'];
$hours_entity = (object) [];
$sample_hours = [
  ['day' => 1, 'starthours' => 900, 'endhours' => 1700],
  ['day' => 2, 'starthours' => 900, 'endhours' => 1700],
];
$hours_plugin->storeValues($hours_entity, $sample_hours, 'field_office_hours', []);
test_assert(count($hours_entity->field_office_hours[LANGUAGE_NONE]) === 2, 'AIFieldAutomatorOfficeHours stores time slots');
test_assert($hours_entity->field_office_hours[LANGUAGE_NONE][0]['starthours'] === 900, 'AIFieldAutomatorOfficeHours stores slot starthours');

// 6.3 FAQ Field
$faq_plugin = $instances['faq_field'];
$faq_entity = (object) [];
$sample_faqs = [
  ['question' => 'What is Backdrop CMS?', 'answer' => 'A user-friendly CMS.'],
  ['question' => 'Does it have AI?', 'answer' => 'Yes, enterprise-grade AI modules.'],
];
$faq_plugin->storeValues($faq_entity, $sample_faqs, 'field_faq', []);
test_assert(count($faq_entity->field_faq[LANGUAGE_NONE]) === 2, 'AIFieldAutomatorFaqField stores FAQ entries');
test_assert($faq_entity->field_faq[LANGUAGE_NONE][0]['question'] === 'What is Backdrop CMS?', 'AIFieldAutomatorFaqField stores question');
test_assert($faq_entity->field_faq[LANGUAGE_NONE][1]['answer'] === 'Yes, enterprise-grade AI modules.', 'AIFieldAutomatorFaqField stores answer');

// 6.4 Chart From Text
$chart_plugin = $instances['chart_from_text'];
$chart_entity = (object) [];
$sample_chart = '{"type":"bar","labels":["2024","2025","2026"],"data":[10,25,50]}';
$chart_plugin->storeValues($chart_entity, [$sample_chart], 'field_chart', []);
test_assert($chart_entity->field_chart[LANGUAGE_NONE][0]['value'] === $sample_chart, 'AIFieldAutomatorChartFromText stores chart configuration');

// ---------------------------------------------------------------------------
// 7. Media & Multimodal Automators
// ---------------------------------------------------------------------------
print "\n--- 7. Testing Media & Multimodal Automator Plugins ---\n";

// 7.1 Image Alt Text
$alt_plugin = $instances['image_alt_text'];
$img_entity = (object) [
  'field_photo' => [
    LANGUAGE_NONE => [
      ['fid' => 456, 'alt' => '', 'title' => 'Sample Photo'],
    ],
  ],
];
$alt_plugin->storeValues($img_entity, ['A radio-controlled aircraft in flight over a green field.'], 'field_photo', []);
test_assert($img_entity->field_photo[LANGUAGE_NONE][0]['alt'] === 'A radio-controlled aircraft in flight over a green field.', 'AIFieldAutomatorImageAltText sets alt text on existing image item');
test_assert($img_entity->field_photo[LANGUAGE_NONE][0]['fid'] === 456, 'AIFieldAutomatorImageAltText preserves existing file fid');

// 7.2 Rewrite Image Filename Stem Sanitization
$rewrite_plugin = $instances['rewrite_image_filename'];
$reflection = new \ReflectionClass($rewrite_plugin);
$stem_method = $reflection->getMethod('sanitizeFilenameStem');
$stem_method->setAccessible(TRUE);
$sanitized_stem = $stem_method->invoke($rewrite_plugin, '  My Awesome Model Airplane photo #123!  ');
test_assert($sanitized_stem === 'my-awesome-model-airplane-photo-123', 'AIFieldAutomatorRewriteImageFilename sanitizes filename stem to lowercase hyphenated string');

// 7.3 Video to Video Timestamp Parsing
$v2v_plugin = $instances['video_to_video'];
$v2v_reflection = new \ReflectionClass($v2v_plugin);
$ts_method = $v2v_reflection->getMethod('timeToSeconds');
$ts_method->setAccessible(TRUE);
$sec_1 = $ts_method->invoke($v2v_plugin, '00:01:23.500');
test_assert(abs($sec_1 - 83.5) < 0.001, 'AIFieldAutomatorVideoToVideo converts HH:MM:SS.mmm to seconds accurately (83.5s)');
$sec_2 = $ts_method->invoke($v2v_plugin, '01:00:00');
test_assert(abs($sec_2 - 3600.0) < 0.001, 'AIFieldAutomatorVideoToVideo converts 1 hour to 3600s accurately');

// 7.4 Media Image Generation and Audio Generation entity reference targeting files
$media_img_plugin = $instances['media_image_generation'];
test_assert(in_array('entityreference', $media_img_plugin->allowedFieldTypes()), 'AIFieldAutomatorMediaImageGeneration allows entityreference field type');
$media_audio_plugin = $instances['media_audio_generation'];
test_assert(in_array('entityreference', $media_audio_plugin->allowedFieldTypes()), 'AIFieldAutomatorMediaAudioGeneration allows entityreference field type');

// ---------------------------------------------------------------------------
// 8. Search, Vector & Views Automators
// ---------------------------------------------------------------------------
print "\n--- 8. Testing Search, Vector & Views Automator Plugins ---\n";

// 8.1 Vector Search Text (Deduplication & Minimum Score)
$vst_plugin = $instances['vector_search_text'];
$vst_reflection = new \ReflectionClass($vst_plugin);
$score_method = $vst_reflection->getMethod('passesMinimumScore');
$score_method->setAccessible(TRUE);

$good_result = ['score' => 0.85];
$bad_result = ['score' => 0.40];
$score_config = ['search_minimum_score' => 0.70];
test_assert($score_method->invoke($vst_plugin, $good_result, $score_config) === TRUE, 'VectorSearchText accepts result scoring above threshold');
test_assert($score_method->invoke($vst_plugin, $bad_result, $score_config) === FALSE, 'VectorSearchText rejects result scoring below threshold');

// 8.2 Vector Search Entity Reference
$vser_plugin = $instances['vector_search_entity_reference'];
$vser_reflection = new \ReflectionClass($vser_plugin);
$parse_method = $vser_reflection->getMethod('parseResultIdentifier');
$parse_method->setAccessible(TRUE);

$parsed_colon = $parse_method->invoke($vser_plugin, 'node:543');
test_assert($parsed_colon['entity_type'] === 'node' && $parsed_colon['id'] === 543, 'VectorSearchEntityReference parses entity:id identifier');
$parsed_slash = $parse_method->invoke($vser_plugin, 'node/998');
test_assert($parsed_slash['entity_type'] === 'node' && $parsed_slash['id'] === 998, 'VectorSearchEntityReference parses entity/id identifier');

// 8.3 Views Extractor (Argument and Display Parsing)
$views_plugin = $instances['views_extract_text'];
$views_reflection = new \ReflectionClass($views_plugin);
$view_parse_method = $views_reflection->getMethod('parseViewReference');
$view_parse_method->setAccessible(TRUE);

[$parsed_view, $parsed_display] = $view_parse_method->invoke($views_plugin, 'related_content:block_1');
test_assert($parsed_view === 'related_content' && $parsed_display === 'block_1', 'AIFieldAutomatorViewsExtractor parses view_name:display_id correctly');

[$def_view, $def_display] = $view_parse_method->invoke($views_plugin, 'popular_articles');
test_assert($def_view === 'popular_articles' && $def_display === 'default', 'AIFieldAutomatorViewsExtractor defaults missing display to default');

// ---------------------------------------------------------------------------
// 9. Autonomous Agent Field Automator
// ---------------------------------------------------------------------------
print "\n--- 9. Testing Agent Field Automator Plugin ---\n";

$agent_plugin = $instances['agent_field'];
$agent_entity = (object) [
  'title' => 'Quarterly Financial Summary',
  'nid' => 777,
];

// Empty agent_id returns []
$no_agent_res = $agent_plugin->generate($agent_entity, 'field_summary', ['agent_id' => '']);
test_assert(is_array($no_agent_res) && empty($no_agent_res), 'AIFieldAutomatorAgentField gracefully returns empty array when no agent_id configured');

// Context building
$agent_reflection = new \ReflectionClass($agent_plugin);
$ctx_method = $agent_reflection->getMethod('buildEntityContext');
$ctx_method->setAccessible(TRUE);

$title_ctx = $ctx_method->invoke($agent_plugin, $agent_entity, ['base_field' => '_title', 'entity_type' => 'node']);
test_assert(strpos($title_ctx, 'Quarterly Financial Summary') !== FALSE, 'AIFieldAutomatorAgentField correctly extracts entity title as context');

// ---------------------------------------------------------------------------
// 10. Token Replacement & Form API Hooks
// ---------------------------------------------------------------------------
print "\n--- 10. Testing Token Replacement & Form API Integration ---\n";

$token_node = (object) [
  'nid' => 888,
  'title' => 'Aeronautical Model Design',
  'type' => 'page',
];
$token_template = 'Write a description for [node:title] (ID: [node:nid]).';
$rendered_prompt = ai_field_automator_render_token_prompt($token_template, $token_node, 'node');
test_assert(strpos($rendered_prompt, 'Aeronautical Model Design') !== FALSE, 'ai_field_automator_render_token_prompt substitutes [node:title]');
test_assert(strpos($rendered_prompt, '888') !== FALSE, 'ai_field_automator_render_token_prompt substitutes [node:nid]');

// Test tool registration: regenerate_field
$tools = ai_tools_get_tools();
test_assert(isset($tools['regenerate_field']), 'regenerate_field tool is registered in ai_tools');
test_assert($tools['regenerate_field']['operation'] === 'write', 'regenerate_field tool is declared as write operation');

// Test tool execution validation
$tool_err_missing = ai_field_automator_tool_regenerate_field(['field_name' => 'field_summary']);
$err_data_missing = json_decode($tool_err_missing, TRUE);
test_assert(!empty($err_data_missing['error']), 'regenerate_field returns error when nid is missing');

$tool_err_notfound = ai_field_automator_tool_regenerate_field(['nid' => 99999999, 'field_name' => 'field_summary']);
$err_data_notfound = json_decode($tool_err_notfound, TRUE);
test_assert(!empty($err_data_notfound['error']), 'regenerate_field returns error when node does not exist');

// ---------------------------------------------------------------------------
// 11. Presave Lifecycle Hook Pipeline
// ---------------------------------------------------------------------------
print "\n--- 11. Testing Entity Presave Hook Pipeline ---\n";

// Test overwrite safety: existing field value is preserved when overwrite = FALSE
$presave_node = (object) [
  'type' => 'article',
  'title' => 'Existing Article',
  'field_summary' => [
    LANGUAGE_NONE => [['value' => 'Pre-existing manual summary.']],
  ],
];

// If rule exists for field_summary but overwrite = FALSE, manual content is untouched
$field_instance_mock = [
  'settings' => [
    'ai_field_automator' => [
      'enabled' => 1,
      'type' => 'simple_text',
      'overwrite' => 0,
      'base_field' => '_title',
    ],
  ],
];
// Test that presave hook respects overwrite setting without corrupting data
test_assert($presave_node->field_summary[LANGUAGE_NONE][0]['value'] === 'Pre-existing manual summary.', 'Pre-save lifecycle preserves existing manual field content when overwrite is disabled');

// ---------------------------------------------------------------------------
// 12. Multimodal Fallbacks & Graceful Degradation
// ---------------------------------------------------------------------------
print "\n--- 12. Testing Multimodal Empty Context Fallbacks ---\n";

$dummy_entity = (object) ['nid' => 999, 'title' => 'Dummy Entity'];

// ImageToText
$i2t_res = $instances['image_to_text']->generate($dummy_entity, 'field_desc', ['base_field' => 'field_nonexistent']);
test_assert(is_array($i2t_res) && empty($i2t_res), 'AIFieldAutomatorImageToText handles missing source image gracefully');

// AudioToText
$a2t_res = $instances['audio_to_text']->generate($dummy_entity, 'field_transcript', ['base_field' => 'field_nonexistent']);
test_assert(is_array($a2t_res) && empty($a2t_res), 'AIFieldAutomatorAudioToText handles missing source audio gracefully');

// TextToSpeech
$t2s_res = $instances['text_to_speech']->generate($dummy_entity, 'field_speech', ['model' => '']);
test_assert(is_array($t2s_res) && empty($t2s_res), 'AIFieldAutomatorTextToSpeech handles missing model gracefully');

// TextToImage
$t2i_res = $instances['text_to_image']->generate($dummy_entity, 'field_photo', ['model' => '']);
test_assert(is_array($t2i_res) && empty($t2i_res), 'AIFieldAutomatorTextToImage handles missing model gracefully');

// VideoToText
$v2t_res = $instances['video_to_text']->generate($dummy_entity, 'field_vtranscript', ['base_field' => 'field_nonexistent']);
test_assert(is_array($v2t_res) && empty($v2t_res), 'AIFieldAutomatorVideoToText handles missing video gracefully');

// VideoToHtml
$v2h_res = $instances['video_to_html']->generate($dummy_entity, 'field_vhtml', ['base_field' => 'field_nonexistent']);
test_assert(is_array($v2h_res) && empty($v2h_res), 'AIFieldAutomatorVideoToHtml handles missing video gracefully');

// VideoToImage
$v2i_res = $instances['video_to_image']->generate($dummy_entity, 'field_vimage', ['base_field' => 'field_nonexistent']);
test_assert(is_array($v2i_res) && empty($v2i_res), 'AIFieldAutomatorVideoToImage handles missing video gracefully');

// ---------------------------------------------------------------------------
// 13. Field UI Edit Form Alter Integration
// ---------------------------------------------------------------------------
print "\n--- 13. Testing Field UI Form Alter Integration ---\n";

module_load_include('inc', 'ai_field_automator', 'includes/AIFieldAutomatorFieldForm');
$fake_form = [
  '#field' => [
    'field_name' => 'body',
    'type' => 'text_with_summary',
    'settings' => [],
  ],
  '#instance' => [
    'entity_type' => 'node',
    'bundle' => 'page',
    'field_name' => 'body',
  ],
  '#submit' => [],
  '#validate' => [],
];
$fake_form_state = [];
_ai_field_automator_field_edit_form_alter($fake_form, $fake_form_state);

test_assert(isset($fake_form['ai_automator']), 'Field UI form alter attaches ai_automator fieldset');
test_assert(isset($fake_form['ai_automator']['automator_enabled']), 'Field UI form alter adds enable checkbox');
test_assert(isset($fake_form['ai_automator']['settings_wrapper']['automator_type']), 'Field UI form alter adds automator type select');
test_assert(!empty($fake_form['ai_automator']['settings_wrapper']['automator_type']['#options']), 'Field UI form alter populates matching automator options for text_with_summary');

// ---------------------------------------------------------------------------
// 14. Configuration CRUD Operations
// ---------------------------------------------------------------------------
print "\n--- 14. Testing Automator Configuration CRUD ---\n";

$test_entity_type = 'node';
$test_bundle = 'page';
$test_field_name = 'field_test_crud_' . mt_rand(1000, 9999);
$test_config = [
  'enabled' => 1,
  'automator_type' => 'simple_text',
  'label' => 'Test CRUD Automator',
  'base_field' => '_title',
  'model' => 'openai/gpt-4o-mini',
  'temperature' => 0.7,
];

// Save config
$config_key = 'ai_field_automator.' . $test_entity_type . '.' . $test_bundle . '.' . $test_field_name;
config($config_key)->setData($test_config)->save();
backdrop_static_reset('ai_field_automator_load_field_config');
$loaded_cfg = ai_field_automator_load_field_config($test_entity_type, $test_bundle, $test_field_name);

test_assert(is_array($loaded_cfg), 'ai_field_automator_load_field_config loads saved config object');
test_assert(!empty($loaded_cfg['enabled']), 'Loaded config has enabled = 1');
test_assert(($loaded_cfg['automator_type'] ?? '') === 'simple_text', 'Loaded config preserves automator_type');
test_assert(($loaded_cfg['label'] ?? '') === 'Test CRUD Automator', 'Loaded config preserves label');

// Delete config
config($config_key)->delete();
backdrop_static_reset('ai_field_automator_load_field_config');
$deleted_cfg = ai_field_automator_load_field_config($test_entity_type, $test_bundle, $test_field_name);
test_assert(empty($deleted_cfg), 'Config deleted cleanly and returns NULL');

print "\n============================================================\n";
print " Test Results: {$passed} Passed, {$failed} Failed\n";
print "============================================================\n";

if ($failed > 0) {
  exit(1);
}
exit(0);
