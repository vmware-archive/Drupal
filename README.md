
Pivotal CF + Drupal
================================

Introduction
------------

Pivotal CF™ is the leading enterprise PaaS, powered by Cloud Foundry. It delivers an always-available, turnkey experience for scaling and updating PaaS on the private cloud. Pivotal CF Elastic Runtime Service provides a complete, scalable runtime environment, extensible to most modern frameworks or languages running on Linux.

Drupal is an open source content management platform supporting a variety of websites ranging from personal weblogs to large community-driven websites. For more information, see the Drupal website at http://drupal.org/, and join the Drupal community at http://drupal.org/community.

Installation
------------

Installation Prerequisites:
 * Pivotal CF v1.2 or greater
 * Pivotal RiakCS service deployed
 * Pivotal MySQL service deployed
 * PHP/Varnish buildpack installed, found here - https://github.com/azwickey-pivotal/cf-php-build-pack
 
Installation instructions:
 * Create services
 * update manfests
 * Push applications
 * Updated proxy variable
 * Add bucket route
 * Configure S3FS
 * Configure filesystem and content type
 * Test
