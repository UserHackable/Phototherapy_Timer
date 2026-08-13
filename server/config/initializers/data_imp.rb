# frozen_string_literal: true

# Seed/import files live next to users.yaml. .yml uses the same parser as .yaml.
DataImp.root = Rails.root
DataImp.data_dir = Rails.root.join("db/data")
DataImp::Parser.const_set(:Yml, DataImp::Parser::Yaml) unless DataImp::Parser.const_defined?(:Yml)
