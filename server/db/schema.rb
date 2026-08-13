# This file is auto-generated from the current state of the database. Instead
# of editing this file, please use the migrations feature of Active Record to
# incrementally modify your database, and then regenerate this schema definition.
#
# This file is the source Rails uses to define your schema when running `bin/rails
# db:schema:load`. When creating a new database, `bin/rails db:schema:load` tends to
# be faster and is potentially less error prone than running all of your
# migrations from scratch. Old migrations may fail to apply correctly if those
# migrations use external dependencies or application code.
#
# It's strongly recommended that you check this file into your version control system.

ActiveRecord::Schema[8.1].define(version: 2026_08_13_060000) do
  create_table "devices", force: :cascade do |t|
    t.datetime "created_at", null: false
    t.string "firmware_app"
    t.string "firmware_version"
    t.string "identity"
    t.string "ip"
    t.json "last_status"
    t.datetime "last_status_at"
    t.datetime "updated_at", null: false
    t.index ["identity"], name: "index_devices_on_identity", unique: true
    t.index ["ip"], name: "index_devices_on_ip"
  end

  create_table "exposures", force: :cascade do |t|
    t.datetime "created_at", null: false
    t.integer "duration_seconds", null: false
    t.integer "skin_type_id"
    t.datetime "started_at", null: false
    t.integer "therapy_type_id"
    t.datetime "updated_at", null: false
    t.integer "user_id", null: false
    t.index ["skin_type_id"], name: "index_exposures_on_skin_type_id"
    t.index ["therapy_type_id"], name: "index_exposures_on_therapy_type_id"
    t.index ["user_id", "started_at"], name: "index_exposures_on_user_id_and_started_at"
    t.index ["user_id"], name: "index_exposures_on_user_id"
  end

  create_table "sessions", force: :cascade do |t|
    t.datetime "created_at", null: false
    t.string "ip_address"
    t.datetime "updated_at", null: false
    t.string "user_agent"
    t.integer "user_id", null: false
    t.index ["user_id"], name: "index_sessions_on_user_id"
  end

  create_table "skin_types", force: :cascade do |t|
    t.datetime "created_at", null: false
    t.text "description", null: false
    t.integer "initial_seconds"
    t.integer "max_seconds"
    t.integer "number", null: false
    t.string "roman", null: false
    t.integer "step_seconds"
    t.datetime "updated_at", null: false
    t.index ["number"], name: "index_skin_types_on_number", unique: true
    t.index ["roman"], name: "index_skin_types_on_roman", unique: true
  end

  create_table "therapy_types", force: :cascade do |t|
    t.datetime "created_at", null: false
    t.text "description", null: false
    t.integer "initial_seconds"
    t.integer "max_seconds"
    t.string "name", null: false
    t.string "slug", null: false
    t.integer "step_seconds"
    t.datetime "updated_at", null: false
    t.boolean "uses_skin_type", default: false, null: false
    t.index ["slug"], name: "index_therapy_types_on_slug", unique: true
  end

  create_table "user_therapies", force: :cascade do |t|
    t.datetime "created_at", null: false
    t.integer "skin_type_id"
    t.integer "therapy_type_id", null: false
    t.datetime "updated_at", null: false
    t.integer "user_id", null: false
    t.index ["skin_type_id"], name: "index_user_therapies_on_skin_type_id"
    t.index ["therapy_type_id"], name: "index_user_therapies_on_therapy_type_id"
    t.index ["user_id", "therapy_type_id"], name: "index_user_therapies_on_user_id_and_therapy_type_id", unique: true
    t.index ["user_id"], name: "index_user_therapies_on_user_id"
  end

  create_table "users", force: :cascade do |t|
    t.datetime "created_at", null: false
    t.string "email_address", null: false
    t.string "name", null: false
    t.string "password_digest", null: false
    t.datetime "updated_at", null: false
    t.index ["email_address"], name: "index_users_on_email_address", unique: true
    t.index ["name"], name: "index_users_on_name"
  end

  add_foreign_key "exposures", "skin_types"
  add_foreign_key "exposures", "therapy_types"
  add_foreign_key "exposures", "users"
  add_foreign_key "sessions", "users"
  add_foreign_key "user_therapies", "skin_types"
  add_foreign_key "user_therapies", "therapy_types"
  add_foreign_key "user_therapies", "users"
end
