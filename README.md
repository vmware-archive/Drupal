
Pivotal CF + Drupal
===================

Introduction
------------

Pivotal CF™ is the leading enterprise PaaS, powered by Cloud Foundry. It delivers an always-available, turnkey experience for scaling and updating PaaS on the private cloud. Pivotal CF Elastic Runtime Service provides a complete, scalable runtime environment, extensible to most modern frameworks or languages running on Linux.

Drupal is an open source content management platform supporting a variety of websites ranging from personal weblogs to large community-driven websites. For more information, see the Drupal website at http://drupal.org/, and join the Drupal community at http://drupal.org/community.

Installation
------------

###Installation Prerequisites
 * Pivotal CF v1.2 or greater
 * Pivotal Riak-CS service deployed
 * Pivotal MySQL service deployed
 * PHP/Varnish buildpack installed, found here - https://github.com/azwickey-pivotal/cf-php-build-pack
 
###Installation instructions:
 * Create services required by Drupal
 
  ```
  $ cf create-service p-mysql 100mb-dev drupal-db
  $ cf create-service p-riakcs developer drupal-s3
  ```
 * Push applications to Cloudfoundry
 
  ```
  $ cf push
  ```
 * Update the deployment manifest with environment-specifc values
  
 First retireve the S3 bucket created for the drupal application.  This can be obtained through the env.log file within the VCAP_SERVICES variable.  The bucket name is the last part of the uri credential

    ```
    $ cf files drupal /logs/env.log
    VCAP_SERVICES={"p-riakcs":[{"name":"drupal-s3","label":"p-riakcs","tags":["riak-cs","s3"],"plan":"developer","credentials":{"uri":"https://XHCE0E1RAVBI99_FJL_5:7AdvdTSBiYwmySKOkoKCyGFkAVHxuaeBg0xTig%3D%3D@p-riakcs.cloudfoundry.dyndns.org/service-instance-423086ed-9167-4026-add2-d734bfb0b2e5","access_key_id":"XHCE0E1RAVBI99_FJL_5","secret_access_key":"7AdvdTSBiYwmySKOkoKCyGFkAVHxuaeBg0xTig=="}}],"p-mysql":[{"name":"drupal-db","label":"p-mysql","tags":["mysql","relational"],"plan":"100mb-dev","credentials":{"hostname":"10.0.0.103","port":3306,"name":"cf_27742bc8_369a_4050_ad53_e85038aa5a35","username":"FEotz981XvDEQXLI","password":"lAXIntesmrowz1hb","uri":"mysql://FEotz981XvDEQXLI:lAXIntesmrowz1hb@10.0.0.103:3306/cf_27742bc8_369a_4050_ad53_e85038aa5a35?reconnect=true","jdbcUrl":"jdbc:mysql://FEotz981XvDEQXLI:lAXIntesmrowz1hb@10.0.0.103:3306/cf_27742bc8_369a_4050_ad53_e85038aa5a35"}}]}
    ```
    
 Next, input this value into S3_BUCKET env variable in the deployment manifest, manifest.yml.  Additionally, update the CF_FQND variable to reflect your cloudfoundry domain.
 
   ```
    env:
      S3_BUCKET: YOUR S3 BUCKET HERE
      CF_FQDN: YOUR CF DOMAIN HERE
   ```
 * Drupal must address Riak-CS with the format of http://$BUCKET_NAME.$CF_DOMAIN.  In order to support this format we must add a Cloudfoundry route to the s3 proxy application that represents our bucket name.
  
  ```
  $ cf map-route s3 cloudfoundry.dyndns.org -n service-instance-423086ed-9167-4026-add2-d734bfb0b2e5
  ```
 * Log into Drupal and enable the S3FS module from the modules menu.  Werify that the S3FS Drupal module can connect to Riak-CS by navigating to Configuration < S3 File System Settings.
 * Configure filesystem default download type to "Amazon Simple Storage Service".  
 * Configure a Drupal content type the contains a field that must be stored on a filesystem, such as an image field, to use "S3 File System" as the defaul tupload location.
